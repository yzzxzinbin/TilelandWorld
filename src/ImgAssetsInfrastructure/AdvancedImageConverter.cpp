#include "AdvancedImageConverter.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iostream>
#include <future>

// Helper macros for vectorization hints
#ifndef IVDEP
  #if defined(__INTEL_COMPILER)
    #define IVDEP _Pragma("ivdep")
  #elif defined(__GNUC__) || defined(__clang__)
    #define IVDEP _Pragma("GCC ivdep")
  #elif defined(_MSC_VER)
    #define IVDEP __pragma(loop(ivdep))
  #else
    #define IVDEP
  #endif
#endif

namespace TilelandWorld {

    // --- Resampling Implementation ---

    static inline uint32_t sum_u8(const uint8_t* ptr, int len) {
        uint32_t s = 0;
        for (int i = 0; i < len; ++i) s += ptr[i];
        return s;
    }

    static inline void sum_u8_pair(const uint8_t* base, int off0, int off1, int len, uint32_t &s0, uint32_t &s1) {
        s0 = sum_u8(base + off0, len);
        s1 = sum_u8(base + off1, len);
    }

    struct Run { int start; int end; int len; };

    static void flatten_to_planes(const RawImage &img, std::vector<uint8_t> &pr, std::vector<uint8_t> &pg, std::vector<uint8_t> &pb, TaskSystem &pool, int tile_h, const std::function<void(double)>& progress) {
        int w = img.width, h = img.height;
        pr.resize((size_t)w * h);
        pg.resize((size_t)w * h);
        pb.resize((size_t)w * h);
        tile_h = std::min(tile_h, h);
        if (tile_h <= 0) tile_h = 64;
        int chunks = (h + tile_h - 1) / tile_h;
        
        std::vector<std::future<void>> futs;
        std::atomic<int> completed{0};
        for (int c = 0; c < chunks; ++c) {
            int y0 = c * tile_h;
            int y1 = std::min(h, y0 + tile_h);
            futs.push_back(std::async(std::launch::async, [=, &img, &pr, &pg, &pb, &completed, &progress]() {
                for (int y = y0; y < y1; ++y) {
                    const uint8_t* src = img.data.data() + (size_t)y * img.width * img.channels;
                    uint8_t* rdst = pr.data() + (size_t)y * img.width;
                    uint8_t* gdst = pg.data() + (size_t)y * img.width;
                    uint8_t* bdst = pb.data() + (size_t)y * img.width;
                    for (int x = 0; x < img.width; ++x) {
                        rdst[x] = src[x * img.channels + 0];
                        gdst[x] = src[x * img.channels + 1];
                        bdst[x] = src[x * img.channels + 2];
                    }
                }
                int done = ++completed;
                if (progress) progress((double)done / chunks);
            }));
        }
        for (auto &f : futs) f.get();
    }

    static void horizontal_box_sum(const std::vector<uint8_t> &pr, const std::vector<uint8_t> &pg, const std::vector<uint8_t> &pb,
                                   int w, int h, int out_w,
                                   const std::vector<int> &x0s,
                                   const std::vector<Run> &runs,
                                   std::vector<uint32_t> &hr, std::vector<uint32_t> &hg, std::vector<uint32_t> &hb,
                                   int tile_h_rows, const std::function<void(double)>& progress) {
        hr.resize((size_t)h * out_w);
        hg.resize((size_t)h * out_w);
        hb.resize((size_t)h * out_w);
        tile_h_rows = std::min(tile_h_rows, h);
        if (tile_h_rows <= 0) tile_h_rows = 64;
        int chunks = (h + tile_h_rows - 1) / tile_h_rows;
        std::vector<std::future<void>> futs;
        
        std::atomic<int> completed{0};
        for (int c = 0; c < chunks; ++c) {
            int y0 = c * tile_h_rows;
            int y1 = std::min(h, y0 + tile_h_rows);
            futs.push_back(std::async(std::launch::async, [=, &pr, &pg, &pb, &hr, &hg, &hb, &x0s, &runs, &completed, &progress]() {
                for (int y = y0; y < y1; ++y) {
                    const uint8_t* rowR = pr.data() + (size_t)y * w;
                    const uint8_t* rowG = pg.data() + (size_t)y * w;
                    const uint8_t* rowB = pb.data() + (size_t)y * w;
                    uint32_t* dstR = hr.data() + (size_t)y * out_w;
                    uint32_t* dstG = hg.data() + (size_t)y * out_w;
                    uint32_t* dstB = hb.data() + (size_t)y * out_w;
                    for (const auto &run : runs) {
                        int len = run.len;
                        int bx = run.start;
                        int end = run.end;
                        for (; bx + 1 < end; bx += 2) {
                            uint32_t sR0, sR1, sG0, sG1, sB0, sB1;
                            sum_u8_pair(rowR, x0s[bx], x0s[bx+1], len, sR0, sR1);
                            sum_u8_pair(rowG, x0s[bx], x0s[bx+1], len, sG0, sG1);
                            sum_u8_pair(rowB, x0s[bx], x0s[bx+1], len, sB0, sB1);
                            dstR[bx] = sR0; dstR[bx+1] = sR1;
                            dstG[bx] = sG0; dstG[bx+1] = sG1;
                            dstB[bx] = sB0; dstB[bx+1] = sB1;
                        }
                        if (bx < end) {
                            int x0 = x0s[bx];
                            dstR[bx] = sum_u8(rowR + x0, len);
                            dstG[bx] = sum_u8(rowG + x0, len);
                            dstB[bx] = sum_u8(rowB + x0, len);
                        }
                    }
                }
                int done = ++completed;
                if (progress) progress((double)done / chunks);
            }));
        }
        for (auto &f : futs) f.get();
    }

    BlockPlanes AdvancedImageConverter::resampleToPlanes(const RawImage& img, int out_w, int out_h, TaskSystem& taskSystem,
                                                        const std::function<void(double)>& stageProgress) {
        BlockPlanes out;
        out.width = out_w;
        out.height = out_h;
        out.r.resize(out_w * out_h);
        out.g.resize(out_w * out_h);
        out.b.resize(out_w * out_h);
        
        if (img.width <= 0 || img.height <= 0 || out_w <= 0 || out_h <= 0) return out;

        if (stageProgress) stageProgress(0.05);
        std::vector<int> x0s(out_w), x1s(out_w);
        for (int bx=0; bx<out_w; ++bx) {
            int x0 = (int)std::floor((double)bx * img.width / out_w);
            int x1 = (int)std::ceil((double)(bx+1) * img.width / out_w);
            x0s[bx] = std::max(0, std::min(img.width, x0));
            x1s[bx] = std::max(0, std::min(img.width, x1));
        }

        std::vector<Run> runs;
        if (out_w > 0) {
            int cur_len = x1s[0] - x0s[0];
            int run_start = 0;
            for (int bx=1; bx<=out_w; ++bx) {
                int len = (bx==out_w) ? -1 : (x1s[bx] - x0s[bx]);
                if (len != cur_len) {
                    runs.push_back(Run{run_start, bx, cur_len});
                    run_start = bx;
                    cur_len = len;
                }
            }
        }

        std::vector<int> y0s(out_h), y1s(out_h);
        for (int by=0; by<out_h; ++by) {
            int y0 = (int)std::floor((double)by * img.height / out_h);
            int y1 = (int)std::ceil((double)(by+1) * img.height / out_h);
            y0s[by] = std::max(0, std::min(img.height, y0));
            y1s[by] = std::max(0, std::min(img.height, y1));
        }

        if (stageProgress) stageProgress(0.05);
        std::vector<uint8_t> pr, pg, pb;
        flatten_to_planes(img, pr, pg, pb, taskSystem, 64, [&](double p){ if (stageProgress) stageProgress(0.05 + 0.1 * p); });
        
        std::vector<uint32_t> hr, hg, hb;
        horizontal_box_sum(pr, pg, pb, img.width, img.height, out_w, x0s, runs, hr, hg, hb, 64, [&](double p){ if (stageProgress) stageProgress(0.15 + 0.15 * p); });
        if (stageProgress) stageProgress(0.3);

        int tile_h_rows = 64;
        int num_chunks = (out_h + tile_h_rows - 1) / tile_h_rows;
        std::vector<std::future<void>> sampleFuts;
        
        std::atomic<int> completedChunks{0};
        for (int c=0;c<num_chunks;++c) {
            int by0 = c * tile_h_rows;
            int by1 = std::min(out_h, by0 + tile_h_rows);
            sampleFuts.push_back(std::async(std::launch::async, [=, &out, &hr, &hg, &hb, &x0s, &x1s, &y0s, &y1s, &completedChunks, &stageProgress]() {
                for (int by = by0; by < by1; ++by) {
                    int y0 = y0s[by];
                    int y1 = y1s[by];
                    IVDEP
                    for (int bx = 0; bx < out_w; ++bx) {
                        int count = (x1s[bx] - x0s[bx]) * (y1 - y0);
                        if (count <= 0) count = 1;
                        uint64_t rsum = 0, gsum = 0, bsum = 0;
                        for (int sy = y0; sy < y1; ++sy) {
                            size_t idx = (size_t)sy * out_w + bx;
                            rsum += hr[idx];
                            gsum += hg[idx];
                            bsum += hb[idx];
                        }
                        size_t idx_out = (size_t)by * out_w + bx;
                        out.r[idx_out] = (int)(rsum / count);
                        out.g[idx_out] = (int)(gsum / count);
                        out.b[idx_out] = (int)(bsum / count);
                    }
                }
                int done = ++completedChunks;
                if (stageProgress) stageProgress(0.3 + 0.7 * (double)done / num_chunks);
            }));
        }
        for (auto &f : sampleFuts) f.get();

        return out;
    }

    // --- Rendering Implementation ---

    struct GDesc { int code; enum {H, V, Q, F, S} type; int level; int qidx; };
    static const std::vector<GDesc> kGlyphs = [](){
        std::vector<GDesc> g;
        g.push_back({0x2588, GDesc::F, 0,0}); // full
        g.push_back({0x20, GDesc::S, 0, 0}); // space
        // quadrants
        g.push_back({0x2598, GDesc::Q, 0, 0});
        g.push_back({0x259D, GDesc::Q, 0, 1});
        g.push_back({0x2596, GDesc::Q, 0, 2});
        g.push_back({0x259E, GDesc::Q, 0, 3});
        // horizontals
        for (int level=8; level>=1; --level) g.push_back({0x2580 + level, GDesc::H, level,0});
        // verticals
        const int vert_codes[8] = {0x258F,0x258E,0x258D,0x258C,0x258B,0x258A,0x2589,0x2588};
        for (int i=7;i>=0;--i) g.push_back({vert_codes[i], GDesc::V, 8-i,0});
        return g;
    }();

    static std::string codepoint_to_utf8(int code) {
        std::string s;
        if (code <= 0x7F) s.push_back((char)code);
        else if (code <= 0x7FF) {
            s.push_back((char)(0xC0 | ((code >> 6) & 0x1F)));
            s.push_back((char)(0x80 | (code & 0x3F)));
        } else if (code <= 0xFFFF) {
            s.push_back((char)(0xE0 | ((code >> 12) & 0x0F)));
            s.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
            s.push_back((char)(0x80 | (code & 0x3F)));
        } else {
            s.push_back((char)(0xF0 | ((code >> 18) & 0x07)));
            s.push_back((char)(0x80 | ((code >> 12) & 0x3F)));
            s.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
            s.push_back((char)(0x80 | (code & 0x3F)));
        }
        return s;
    }

    ImageAsset AdvancedImageConverter::renderToAsset(const BlockPlanes& highres, int outW, int outH, const Options& opts, TaskSystem& taskSystem,
                                                     const std::function<void(double)>& stageProgress) {
        ImageAsset asset(outW, outH);
        const int SUB_W = 8, SUB_H = 8;
        int high_w = highres.width;
        int high_h = highres.height;

        // Build integral images
        std::vector<uint64_t> sumR((high_w+1)*(high_h+1)), sumG((high_w+1)*(high_h+1)), sumB((high_w+1)*(high_h+1));
        std::vector<uint64_t> sumR2((high_w+1)*(high_h+1)), sumG2((high_w+1)*(high_h+1)), sumB2((high_w+1)*(high_h+1));

        if (stageProgress) stageProgress(0.01);
        for (int y=0;y<high_h;++y) {
            uint64_t rowR=0,rowG=0,rowB=0;
            uint64_t rowR2=0,rowG2=0,rowB2=0;
            for (int x=0;x<high_w;++x) {
                size_t idx = (size_t)y * high_w + x;
                int r = highres.r[idx];
                int g = highres.g[idx];
                int b = highres.b[idx];
                rowR += r; rowG += g; rowB += b;
                rowR2 += (uint64_t)r * (uint64_t)r;
                rowG2 += (uint64_t)g * (uint64_t)g;
                rowB2 += (uint64_t)b * (uint64_t)b;
                int ii = (y+1)*(high_w+1) + (x+1);
                int ii_up = (y)*(high_w+1) + (x+1);
                sumR[ii] = sumR[ii_up] + rowR;
                sumG[ii] = sumG[ii_up] + rowG;
                sumB[ii] = sumB[ii_up] + rowB;
                sumR2[ii] = sumR2[ii_up] + rowR2;
                sumG2[ii] = sumG2[ii_up] + rowG2;
                sumB2[ii] = sumB2[ii_up] + rowB2;
            }
            if (stageProgress && (y % 64 == 0)) stageProgress(0.01 + 0.14 * (double)y / high_h);
        }
        if (stageProgress) stageProgress(0.15);

        auto rect_sum3 = [&](const std::vector<uint64_t> &S, int x0,int y0,int x1,int y1)->uint64_t{
            uint64_t A = S[y0*(high_w+1)+x0];
            uint64_t B = S[y0*(high_w+1)+x1];
            uint64_t C = S[y1*(high_w+1)+x0];
            uint64_t D = S[y1*(high_w+1)+x1];
            return D + A - B - C;
        };

        int threads = std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::future<void>> futs;
        std::atomic<int> completedRows{0};

        for (int tid=0; tid<threads; ++tid) {
            int row0 = (outH * tid) / threads;
            int row1 = (outH * (tid+1)) / threads;
            
            futs.push_back(std::async(std::launch::async, [=, &asset, &sumR, &sumG, &sumB, &sumR2, &sumG2, &sumB2, &completedRows, &stageProgress]() {
                for (int by=row0; by<row1; ++by) {
                    for (int bx=0; bx<outW; ++bx) {
                        int x0c = bx*SUB_W, y0c = by*SUB_H, x1c = x0c + SUB_W, y1c = y0c + SUB_H;
                        uint64_t totalR = rect_sum3(sumR, x0c, y0c, x1c, y1c);
                        uint64_t totalG = rect_sum3(sumG, x0c, y0c, x1c, y1c);
                        uint64_t totalB = rect_sum3(sumB, x0c, y0c, x1c, y1c);
                        uint64_t totalR2 = rect_sum3(sumR2, x0c, y0c, x1c, y1c);
                        uint64_t totalG2 = rect_sum3(sumG2, x0c, y0c, x1c, y1c);
                        uint64_t totalB2 = rect_sum3(sumB2, x0c, y0c, x1c, y1c);

                        double best_err = 1e308; 
                        int best_cp = 0x20;
                        int best_fr=0,best_fg=0,best_fb=0,best_br=0,best_bg=0,best_bb=0;
                        uint64_t tot = (uint64_t)SUB_W * SUB_H;
                        
                        int total_avg_r = (int)(totalR / tot);
                        int total_avg_g = (int)(totalG / tot);
                        int total_avg_b = (int)(totalB / tot);

                        for (const auto &gd : kGlyphs) {
                            uint64_t fgR=0,fgG=0,fgB=0; uint64_t fgR2=0,fgG2=0,fgB2=0; uint64_t fgCnt=0;
                            
                            if (gd.type == GDesc::H) {
                                int rows = (int)std::ceil(gd.level * (double)SUB_H / 8.0);
                                int fy0 = y1c - rows, fy1 = y1c;
                                fgCnt = (uint64_t)SUB_W * (fy1 - fy0);
                                fgR = rect_sum3(sumR, x0c, fy0, x1c, fy1);
                                fgG = rect_sum3(sumG, x0c, fy0, x1c, fy1);
                                fgB = rect_sum3(sumB, x0c, fy0, x1c, fy1);
                                fgR2 = rect_sum3(sumR2, x0c, fy0, x1c, fy1);
                                fgG2 = rect_sum3(sumG2, x0c, fy0, x1c, fy1);
                                fgB2 = rect_sum3(sumB2, x0c, fy0, x1c, fy1);
                            } else if (gd.type == GDesc::V) {
                                int cols = (int)std::ceil(gd.level * (double)SUB_W / 8.0);
                                int fx0 = x0c, fx1 = x0c + cols;
                                fgCnt = (uint64_t)(fx1 - fx0) * SUB_H;
                                fgR = rect_sum3(sumR, fx0, y0c, fx1, y1c);
                                fgG = rect_sum3(sumG, fx0, y0c, fx1, y1c);
                                fgB = rect_sum3(sumB, fx0, y0c, fx1, y1c);
                                fgR2 = rect_sum3(sumR2, fx0, y0c, fx1, y1c);
                                fgG2 = rect_sum3(sumG2, fx0, y0c, fx1, y1c);
                                fgB2 = rect_sum3(sumB2, fx0, y0c, fx1, y1c);
                            } else if (gd.type == GDesc::Q) {
                                int qx0 = (gd.qidx % 2) ? (x0c + SUB_W/2) : x0c;
                                int qx1 = qx0 + SUB_W/2;
                                int qy0 = (gd.qidx < 2) ? y0c : (y0c + SUB_H/2);
                                int qy1 = qy0 + SUB_H/2;
                                fgCnt = (uint64_t)(qx1 - qx0) * (qy1 - qy0);
                                fgR = rect_sum3(sumR, qx0, qy0, qx1, qy1);
                                fgG = rect_sum3(sumG, qx0, qy0, qx1, qy1);
                                fgB = rect_sum3(sumB, qx0, qy0, qx1, qy1);
                                fgR2 = rect_sum3(sumR2, qx0, qy0, qx1, qy1);
                                fgG2 = rect_sum3(sumG2, qx0, qy0, qx1, qy1);
                                fgB2 = rect_sum3(sumB2, qx0, qy0, qx1, qy1);
                            } else if (gd.type == GDesc::F) {
                                fgCnt = tot;
                                fgR = totalR; fgG = totalG; fgB = totalB;
                                fgR2 = totalR2; fgG2 = totalG2; fgB2 = totalB2;
                            } else { // space
                                fgCnt = 0; fgR = fgG = fgB = 0; fgR2 = fgG2 = fgB2 = 0;
                            }
                            uint64_t bgCnt = tot - fgCnt;

                            int fr = 0, fgc = 0, fb = 0, br = 0, bgcol = 0, bb = 0;
                            if (fgCnt>0) { fr = (int)(fgR/fgCnt); fgc = (int)(fgG/fgCnt); fb = (int)(fgB/fgCnt); }
                            if (bgCnt>0) { br = (int)((totalR - fgR)/bgCnt); bgcol = (int)((totalG - fgG)/bgCnt); bb = (int)((totalB - fgB)/bgCnt); }
                            
                            int color_diff = abs(fr - br) + abs(fgc - bgcol) + abs(fb - bb);
                            if (color_diff < opts.pruneThreshold) {
                                continue;
                            }

                            double err = 0.0;
                            // Scalar fallback logic
                            if (fgCnt > 0) {
                                double term_fg = (double)fgR * (double)fgR / (double)fgCnt;
                                double term_bg = 0.0;
                                if (bgCnt > 0) {
                                    double bgRsum = (double)(totalR - fgR);
                                    term_bg = bgRsum * bgRsum / (double)bgCnt;
                                }
                                err += (double)totalR2 - term_fg - term_bg;
                            } else {
                                if (bgCnt > 0) err += (double)totalR2 - (double)(totalR * totalR) / (double)bgCnt; else err += (double)totalR2;
                            }
                            if (fgCnt > 0) {
                                double term_fg = (double)fgG * (double)fgG / (double)fgCnt;
                                double term_bg = 0.0;
                                if (bgCnt > 0) { double bgGsum = (double)(totalG - fgG); term_bg = bgGsum * bgGsum / (double)bgCnt; }
                                err += (double)totalG2 - term_fg - term_bg;
                            } else {
                                if (bgCnt > 0) err += (double)totalG2 - (double)(totalG * totalG) / (double)bgCnt; else err += (double)totalG2;
                            }
                            if (fgCnt > 0) {
                                double term_fg = (double)fgB * (double)fgB / (double)fgCnt;
                                double term_bg = 0.0;
                                if (bgCnt > 0) { double bgBsum = (double)(totalB - fgB); term_bg = bgBsum * bgBsum / (double)bgCnt; }
                                err += (double)totalB2 - term_fg - term_bg;
                            } else {
                                if (bgCnt > 0) err += (double)totalB2 - (double)(totalB * totalB) / (double)bgCnt; else err += (double)totalB2;
                            }

                            if (err < best_err) {
                                best_err = err; best_cp = gd.code;
                                if (fgCnt>0) { best_fr = (int)(fgR/fgCnt); best_fg = (int)(fgG/fgCnt); best_fb = (int)(fgB/fgCnt); }
                                if (bgCnt>0) { best_br = (int)((totalR - fgR)/bgCnt); best_bg = (int)((totalG - fgG)/bgCnt); best_bb = (int)((totalB - fgB)/bgCnt); }
                            }
                        }

                        // Set cell
                        RGBColor fg = { (uint8_t)best_fr, (uint8_t)best_fg, (uint8_t)best_fb };
                        RGBColor bg = { (uint8_t)best_br, (uint8_t)best_bg, (uint8_t)best_bb };
                        // Note: AssetManagerScreen uses TuiCell which expects UTF-8 string.
                        // ImageAsset stores std::string character.
                        asset.setCell(bx, by, {codepoint_to_utf8(best_cp), fg, bg});
                    }
                    int doneRows = ++completedRows;
                    if (stageProgress) stageProgress(0.15 + 0.85 * (double)doneRows / outH);
                }
            }));
        }
        for (auto &f : futs) f.get();

        return asset;
    }

    ImageAsset AdvancedImageConverter::renderLow(const BlockPlanes& highres, int outW, int outH, TaskSystem& taskSystem,
                                                const std::function<void(double)>& stageProgress) {
        ImageAsset asset(outW, outH);
        int high_w = highres.width;
        
        // Simple parallel loop
        int threads = std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::future<void>> futs;

        std::atomic<int> completedRows{0};
        for (int tid=0; tid<threads; ++tid) {
            int row0 = (outH * tid) / threads;
            int row1 = (outH * (tid+1)) / threads;
            
            futs.push_back(std::async(std::launch::async, [=, &asset, &highres, &completedRows, &stageProgress]() {
                for (int by=row0; by<row1; ++by) {
                    for (int bx=0; bx<outW; ++bx) {
                        long long rsum=0, gsum=0, bsum=0; 
                        int count=0;
                        for (int dy=0; dy<8; ++dy) {
                            for (int dx=0; dx<8; ++dx) {
                                int sx = bx*8 + dx; 
                                int sy = by*8 + dy;
                                size_t idx = (size_t)sy * high_w + sx;
                                rsum += highres.r[idx];
                                gsum += highres.g[idx];
                                bsum += highres.b[idx];
                                ++count;
                            }
                        }
                        
                        RGBColor bg;
                        if (count > 0) {
                            bg.r = (uint8_t)(rsum / count);
                            bg.g = (uint8_t)(gsum / count);
                            bg.b = (uint8_t)(bsum / count);
                        } else {
                            bg = {0,0,0};
                        }
                        
                        // Low quality uses space with background color
                        asset.setCell(bx, by, {" ", {0,0,0}, bg});
                    }
                    int doneRows = ++completedRows;
                    if (stageProgress) stageProgress((double)doneRows / outH);
                }
            }));
        }
        for (auto &f : futs) f.get();
        
        return asset;
    }

    ImageAsset AdvancedImageConverter::convert(const RawImage& img, const Options& opts, TaskSystem& taskSystem) {
        if (!img.valid) return ImageAsset(0, 0);
        
        // Quantize work: 
        // 1. Resampling: proportional to img.width * img.height
        // 2. Rendering: proportional to targetWidth * targetHeight
        // Stage 1 weight: Source Pixels / 250.0 (slightly more weight than before)
        // Stage 2 weight: Target Pixels * (IsHigh ? 5.0 : 0.1)
        
        double sourceWork = (double)img.width * img.height / 250.0;
        double renderWork = (double)opts.targetWidth * opts.targetHeight;
        if (opts.quality == Options::Quality::High) {
            renderWork *= 5.0; // High quality is expensive
        } else {
            renderWork *= 0.5; // Increased low quality weight slightly for visibility
        }

        double totalWork = sourceWork + renderWork;
        double completedWork = 0;

        auto reportProgress = [&](double stageCompletion, const std::string& stageName) {
            if (opts.onProgress) {
                double stageBase = (stageName == "Resampling") ? 0 : sourceWork;
                double stageScale = (stageName == "Resampling") ? sourceWork : renderWork;
                opts.onProgress(stageBase + stageCompletion * stageScale, totalWork, stageName);
            }
        };

        // 1. Resample to 8x resolution of target
        int highW = opts.targetWidth * 8;
        int highH = opts.targetHeight * 8;
        
        BlockPlanes highres = resampleToPlanes(img, highW, highH, taskSystem, [&](double p){
            reportProgress(p, "Resampling");
        });
        
        // 2. Render using glyph matching or low quality
        if (opts.quality == Options::Quality::High) {
            return renderToAsset(highres, opts.targetWidth, opts.targetHeight, opts, taskSystem, [&](double p){
                reportProgress(p, "Rendering");
            });
        } else {
            return renderLow(highres, opts.targetWidth, opts.targetHeight, taskSystem, [&](double p){
                reportProgress(p, "Rendering");
            });
        }
    }

}

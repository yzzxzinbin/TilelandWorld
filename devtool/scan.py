import os
import re
from typing import List, Dict, Tuple
from collections import defaultdict
from parastate import ParserState
from SymbolScanner import CppSymbolScanner
import colorama  # Import colorama
import logging  # Import logging
from callgraph import CallGraphAnalyzer  # Import CallGraphAnalyzer

# --- Logging Setup ---
log_file = "scan_debug.log"
# Configure logging to file, ensuring it's overwritten each run (filemode='w')
logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s - %(levelname)s - %(message)s",
    filename=log_file,
    filemode="w",
)
# --- End Logging Setup ---


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("用法: python cpp_scanner.py <目录路径>")
        logging.error("Script called without directory path argument.")
        sys.exit(1)

    scanner = CppSymbolScanner()
    target_dir = sys.argv[1]
    print(f"开始扫描目录: {target_dir}")
    logging.info(f"Target directory set to: {target_dir}")
    scanner.scan_directory(target_dir)
    scanner.print_summary()

    # --- 新增：调用图分析 ---
    if scanner.symbols and scanner.call_graph:
        analyzer = CallGraphAnalyzer(scanner.symbols, scanner.call_graph)
        analyzer.interactive_trace()
    else:
        print(
            "\nNo symbols or call graph data generated, skipping call chain analysis."
        )
    # --- 结束新增 ---

    print("\n扫描完成。")
    logging.info("Script execution finished.")

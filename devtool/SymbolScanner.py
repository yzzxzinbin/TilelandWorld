import os
import re
from typing import List, Dict, Tuple
from collections import defaultdict
from parastate import ParserState
import colorama  # Import colorama
import logging  # Import logging
from callgraph import CallGraphAnalyzer  # Import CallGraphAnalyzer


class CppSymbolScanner:
    def __init__(self):
        # 初始化状态机
        self.state = ParserState()

        # 按状态分组的正则表达式模式
        self.global_patterns = {
            # 全局状态下的匹配规则
            "macro_constant": re.compile(
                r"^\s*#\s*define\s+"  # #define keyword
                r"([a-zA-Z_]\w*)"  # Macro name (Group 1)
                # Negative lookahead for '(', ensure it's not a function-like macro
                r"(?!\()"
                r"\s+"  # Separator space
                # Value (Group 2) - capture non-greedily until end or comment
                r"(.+?)"
                r"\s*(?:$|//|/\*)"  # End of line or start of comment
            ),
            "macro_function": re.compile(
                r"^\s*#\s*define\s+"  # #define keyword
                r"([a-zA-Z_]\w*)"  # Macro name (Group 1)
                r"\("  # Opening parenthesis for arguments
                # Arguments inside parentheses (non-greedy)
                r"[^)]*\)"
                # Optional replacement value (can be complex, capture simply for now)
                r"(.*)"  # Capture the rest (Group 2)
            ),
            "namespace": re.compile(
                r"^\s*namespace\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*(?:\{)?\s*$"
            ),
            "class": re.compile(
                # Allow template<...> before class/struct
                r"(?:template\s*<[^>]+>\s*)?"
                r"class\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*(?:final\s*)?(?::\s*[^{]+)?\s*\{"
            ),
            "struct": re.compile(
                # Allow template<...> before class/struct
                r"(?:template\s*<[^>]+>\s*)?"
                r"struct\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*(?::\s*[^{]+)?\s*\{"
            ),
            "global_function_decl": re.compile(
                # Avoid matching lines starting with #define or friend
                r"^(?!\s*#\s*define|\s*friend)"
                r"(?:template\s*<[^>]+>\s*)?"  # 模板前缀
                r"(?:(?:inline|constexpr|static|virtual|explicit)\s+)*"  # 修饰符
                # Group 1: Return type
                r"((?:[\w:]+)(?:\s*\*+\s*|\s*&\s*|\s+))+\s*"
                # Group 2: Function name
                r"([a-zA-Z_][a-zA-Z0-9_:]*)\s*"
                r"(?!\s*(?:if|for|while|switch)\b)"
                # Group 3: Parameters (captured)
                r"(\([^;{}]*\))\s*(?:const\s*)?(?:noexcept(?:\([^)]*\))?)?\s*;"
            ),
            "global_function_def": re.compile(
                r"^(?!\s*#\s*define)"
                r"(?:template\s*<[^>]+>\s*)?"  # 模板前缀
                r"(?:(?:inline|constexpr|static|virtual|explicit)\s+)*"  # 修饰符
                # 返回类型（严格匹配模板和限定符）
                r"((?:(?:\w+\s*::\s*)*\w+(?:\s*<\s*[^<>]*\s*>)?(?:\s*[*&]\s*)*\s+)+)"
                # 函数名（强制要求至少有一个字符）
                r"((?:[a-zA-Z_][a-zA-Z0-9_]*\s*::\s*)*[a-zA-Z_][a-zA-Z0-9_]*)\s*"
                r"(?!\s*(?:if|for|while|switch)\b)"
                r"\([^{}]*\)\s*(?:const\s*)?(?:noexcept(?:\([^)]*\))?)?\s*\{"
            ),
            "global_variable": re.compile(
                # Avoid matching lines starting with #define
                r"^(?!\s*#\s*define)"
                r"(?:extern\s+)?"  # extern修饰符
                r"(?:(?:const|static|volatile|mutable)\s+)*"  # 其他修饰符
                # More robust type
                r"((?:[\w:]+)(?:\s*\*+\s*|\s*&\s*|\s+))+\s*"
                # 变量名 - Group 2
                r"([a-zA-Z_][a-zA-Z0-9_]*)\s*"
                # Look for array, assignment, comma, or semicolon, but NOT immediately followed by (
                r"(?:\[.*?\])?\s*(?![ \t]*\()[=;,]"
            ),
            # 新增: 枚举类型
            "enum": re.compile(
                r"^\s*enum\s+(?:class|struct\s+)?([a-zA-Z_][a-zA-Z0-9_]*)\s*(?::\s*[^{]+)?\s*\{[^}]*\}\s*;?"
            ),
            # 新增: 联合类型
            "union": re.compile(r"^\s*union\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\{"),
        }

        self.class_patterns = {
            # 类和结构体内的匹配规则
            "member_function_decl": re.compile(
                r"^\s*(?:(?:public|protected|private)\s*:\s*)?"  # 可选的访问修饰符
                r"(?!\s*#\s*define|\s*friend)"  # 排除宏和友元声明
                r"(?:template\s*<[^>]+>\s*)?"  # 可选的模板前缀
                r"(?:(?:inline|constexpr|static|virtual|explicit)\s+)*"  # 修饰符
                # Group 1: Return type (optional for ctor/dtor)
                r"((?:(?:[\w:]+)(?:<[^>]+>)?(?:\s*\*+\s*|\s*&\s*|\s+))*|\~?)?\s*"
                # Group 2: Function name
                r"([a-zA-Z_][a-zA-Z0-9_~]*)\s*"  # 允许 ~ 用于析构函数
                r"(?!\s*(?:if|for|while|switch)\b)"
                # Group 3: Parameters (captured)
                r"(\([^;{}]*\))\s*(?:const\s*)?(?:noexcept(?:\([^)]*\))?)?\s*(?:=\s*0)?\s*;"
            ),
            "member_function_def": re.compile(
                r"^(?:\s*(?:public|protected|private)\s*:\s*)?(?!\s*#\s*define)"
                r"(?:template\s*<[^>]+>\s*)?"
                r"(?:(?:inline|constexpr|static|virtual|explicit)\s+)*"
                # 返回类型 (Group 1)
                r"((?:(?:[\w:]+)(?:\s*\*+\s*|\s*&\s*|\s+))+|\~?)?\s*"
                # 函数名 (Group 3)
                r"([a-zA-Z_][a-zA-Z0-9_~]*)\s*"  # 允许 ~ 用于析构函数
                # 防止匹配 if/for/while 等关键字
                r"(?!\s*(?:if|for|while|switch)\b)"
                # 参数、const、noexcept、函数体开始
                r"\([^{}]*\)\s*(?:const\s*)?(?:noexcept(?:\([^)]*\))?)?\s*\{"
            ),
            "member_variable": re.compile(
                r"^\s*(?:(?:public|protected|private)\s*:\s*)?"  # 可选的访问修饰符
                r"(?!\s*#\s*define|\s*using|\s*typedef|\s*friend|\s*class|\s*struct)"  # 排除特定关键字
                r"(?:(?:static|mutable|const|volatile)\s+)*"  # 可选的修饰符
                # 类型 (Group 1) - 更简化和安全的类型匹配
                r"([\w:<>]+(?:\s*[\*&]\s*|\s+))"  # 简化的类型匹配
                # 变量名 (Group 2)
                r"([a-zA-Z_][a-zA-Z0-9_]*)"
                # 可选的数组声明、初始化或分号
                r"(?:\s*\[\s*\w*\s*\])?\s*(?:=\s*[^;]+)?\s*;"
            ),
            "friend_function_decl": re.compile(
                r"^\s*friend\s+"  # Starts with friend
                r"(?:template\s*<[^>]+>\s*)?"
                r"(?:(?:inline|constexpr|static)\s+)*"  # Modifiers applicable to friend
                # Group 1: Return type
                r"((?:[\w:]+)(?:\s*\*+\s*|\s*&\s*|\s+))+\s*"
                # Group 2: Function name
                r"([a-zA-Z_][a-zA-Z0-9_:]*)\s*"
                r"(?!\s*(?:if|for|while|switch)\b)"
                # Group 3: Parameters (captured)
                r"(\([^;{}]*\))\s*(?:const\s*)?(?:noexcept(?:\([^)]*\))?)?\s*;"
            ),
            "access_modifier": re.compile(r"^\s*(public|protected|private)\s*:"),
            "nested_class": re.compile(
                r"^\s*(?:(?:public|protected|private)\s*:\s*)?\s*"
                r"(?:template\s*<[^>]+>\s*)?"
                r"class\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*(?:final\s*)?(?::\s*[^{]+)?\s*\{"
            ),
            "nested_struct": re.compile(
                r"^\s*(?:(?:public|protected|private)\s*:\s*)?\s*"
                r"(?:template\s*<[^>]+>\s*)?"
                r"struct\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*(?::\s*[^{]+)?\s*\{"
            ),
            # 新增: 嵌套枚举类型
            "nested_enum": re.compile(
                r"^\s*(?:(?:public|protected|private)\s*:\s*)?\s*"
                r"enum\s+(?:class|struct\s+)?([a-zA-Z_][a-zA-Z0-9_]*)\s*(?::\s*[^{]+)?\s*\{[^}]*\}\s*;?"
            ),
            # 新增: 嵌套联合类型
            "nested_union": re.compile(
                r"^\s*(?:(?:public|protected|private)\s*:\s*)?\s*"
                r"union\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\{"
            ),
        }

        self.out_of_class_patterns = {
            # 类外成员函数定义的匹配规则
            "out_of_class_member_def": re.compile(
                r"^(?!\s*#\s*define)"
                r"(?:template\s*<[^>]+>\s*)?"
                r"(?:(?:inline|constexpr|static|virtual|explicit)\s+)*"
                # 返回类型 (Group 1)
                r"((?:(?:[\w:]+)(?:\s*\*+\s*|\s*&\s*|\s+))+|\~?)?\s*"
                # 类名限定符 (Group 2)
                r"((?:[a-zA-Z_][a-zA-Z0-9_]*::)+)"
                # 函数名 (Group 3)
                r"([a-zA-Z_][a-zA-Z0-9_~]*)\s*"
                # 防止匹配 if/for/while 等关键字
                r"(?!\s*(?:if|for|while|switch)\b)"
                # 参数、const、noexcept、函数体开始
                r"\([^{}]*\)\s*(?:const\s*)?(?:noexcept(?:\([^)]*\))?)?\s*\{"
            ),
        }

        self.function_body_patterns = {
            # 函数体内部的匹配规则 (采用更严谨的正则表达式)
            "function_call": re.compile(
                r"""
                (?:^|(?<=[^\w]))   # 匹配必须在行首或前面是一个非单词字符
                # --- 排除控制流关键字 ---
                (?!                # 负向先行断言：确保不是关键字开头 + (
                    (?:if|while|for|switch|catch|return|throw|delete|new)\s*\(
                )
                # --- 匹配函数调用 ---
                (                  # 开始捕获组 1（完整前缀 + 函数名）
                    (?:            # 可选的前缀部分 (非捕获)
                        (?:(?:\w+::)+) | # 选项 1: 仅命名空间 ns1::ns2::
                        (?:(?:\w+::)*\w+(?:\.|->)) # 选项 2: [命名空间::]类/对象.或->
                    )?             # 前缀结束 (可选)
                    (\w+)          # 捕获组 2: 纯函数名 (必须)
                )                  # 结束捕获组 1
                # --- 后置检查 ---
                (?!\s*<[^>]*>\s*\w) # 负向先行断言：排除模板实例化如 `vector<int> v`
                \s*               # 允许函数名和模板/括号间的空格
                (?:<\s*[^<>]*\s*>)?\s* # 可选的模板参数 `<T>`
                \(                # 参数列表开始
                (?:[^()]|\((?:[^()]|\([^()]*\))*\))* # 参数内容（允许嵌套括号）
                \)                # 参数列表结束
                # --- 结尾检查 ---
                (?=\s*(?:[;,:)\]}]|$)) # 正向先行断言：后面必须是有效结束符或行尾
                """,
                re.VERBOSE | re.MULTILINE,  # 使用VERBOSE方便注释, MULTILINE让^匹配行首
            )
        }

        # 组合所有模式供按需选择
        self.all_patterns = {}
        for pattern_dict in [
            self.global_patterns,
            self.class_patterns,
            self.out_of_class_patterns,
            self.function_body_patterns,
        ]:
            self.all_patterns.update(pattern_dict)

        # 其他扫描器状态
        self.symbols = defaultdict(lambda: defaultdict(list))
        # 调用图: { defining_func_full_name: {called_func_name1, ...} }
        self.call_graph = defaultdict(set)

    def reset(self):
        """重置扫描器状态"""
        logging.info("Resetting scanner state.")
        self.state.reset()
        self.symbols = defaultdict(lambda: defaultdict(list))
        self.call_graph = defaultdict(set)  # 重置调用图

    def scan_file(self, file_path: str):
        """扫描单个C++文件"""
        logging.info(f"Starting scan for file: {file_path}")
        try:
            with open(file_path, "r", encoding="utf-8") as f:
                content = f.read()

            # 处理每一行
            lines = content.split("\n")
            logging.debug(f"File '{file_path}' split into {len(lines)} lines.")

            # 重置扫描前的状态机，确保每个文件都从干净状态开始
            self.state.reset()

            for line_num, line in enumerate(lines, 1):
                self._process_line(line, line_num, file_path)

        except UnicodeDecodeError:
            logging.warning(
                f"UnicodeDecodeError: Could not decode file {file_path}, skipping."
            )
            print(f"警告: 无法解码文件 {file_path}，跳过")
        except Exception as e:
            logging.error(
                f"Exception processing file {file_path}: {str(e)}", exc_info=True
            )
            print(f"处理文件 {file_path} 时出错: {str(e)}")
        logging.info(f"Finished scan for file: {file_path}")

    def _strip_string_literals(self, line: str) -> str:
        """
        将字符串和字符字面量的内容替换为空格，保留引号，处理转义。
        """
        stripped_chars = list(line)
        in_double_quotes = False
        in_single_quotes = False
        escaped = False

        for i, char in enumerate(stripped_chars):
            if escaped:
                # 如果前一个字符是反斜杠，则当前字符被转义，重置 escaped 标志
                escaped = False
                if in_double_quotes or in_single_quotes:
                    # 在字符串/字符内部，转义字符本身也替换为空格
                    stripped_chars[i] = " "
                continue  # 跳过转义字符的处理逻辑

            if char == "\\":
                # 遇到反斜杠，设置 escaped 标志
                escaped = True
                if in_double_quotes or in_single_quotes:
                    # 在字符串/字符内部，反斜杠本身替换为空格
                    stripped_chars[i] = " "
                continue  # 跳过后续处理

            if char == '"' and not in_single_quotes:
                in_double_quotes = not in_double_quotes
                continue  # 保留引号本身

            if char == "'" and not in_double_quotes:
                in_single_quotes = not in_single_quotes
                continue  # 保留引号本身

            if in_double_quotes or in_single_quotes:
                # 如果在字符串或字符字面量内部（且不是引号或转义符），替换为空格
                stripped_chars[i] = " "

        result = "".join(stripped_chars)
        if result != line:
            logging.debug(f"Stripped string literals: '{line}' -> '{result}'")
        return result

    def _process_line(self, line: str, line_num: int, file_path: str):
        """处理单行代码，基于状态机逻辑"""
        # --- 新增：移除 // 注释 ---
        line_no_comment = line.split("//", 1)[0]
        # --- 结束新增 ---

        # --- 修改：基于移除注释后的行进行 strip ---
        stripped_line = line_no_comment.strip()
        # --- 结束修改 ---

        logging.debug(f"--- Processing Line {line_num} ({file_path}) ---")
        # --- 修改：记录移除注释后的行 ---
        logging.debug(f"Original line: '{line}'")
        logging.debug(f"Line without // comment: '{line_no_comment}'")
        logging.debug(f"Stripped line (after // removal): '{stripped_line}'")
        # --- 结束修改 ---
        logging.debug(
            f"State: Current={self.state.current_state}, NS={self.state.namespace_stack}, Class={self.state.class_stack}, Braces={len(self.state.brace_stack)}, Pending Lines={len(self.state.pending_lines)}"
        )

        if not stripped_line:
            logging.debug(
                "Line is empty after comment removal and stripping, skipping."
            )
            if self.state.pending_lines:
                logging.debug("Empty line encountered, clearing pending lines.")
                self.state.pending_lines.clear()
            return

        # 如果在多行括号模式，追加当前行并检查是否完成
        # 注意：多行括号模式下，注释移除已在上面完成，这里使用 stripped_line
        if self.state.in_multiline_paren:
            self.state.add_to_multiline_paren(
                stripped_line
            )  # 使用移除注释和strip后的行

            # 分析当前行以更新括号计数 (使用 stripped_line)
            self.state.reset_line_state()
            prev_char = None
            for char in stripped_line:  # 使用 stripped_line
                self.state.process_character(char, prev_char)
                prev_char = char

            # 检查括号是否平衡 (使用 stripped_line)
            if self.state.has_balanced_parens() or stripped_line.endswith(";"):
                # 括号平衡或遇到语句结束符，处理完整的多行内容
                combined_content = (
                    self.state.get_multiline_paren_content()
                )  # 获取的是已处理过的行
                self.state.exit_multiline_paren_mode()

                logging.debug(
                    f"Processing combined multiline content (comments removed per line): '{combined_content}'"
                )

                # ... (后续的多行内容匹配逻辑保持不变，因为 combined_content 已处理) ...
                patterns_to_check = {}
                current_state_for_match = self.state.current_state
                if current_state_for_match == ParserState.FUNCTION_DEF:
                    # 在函数体内，应该仅使用函数体内的模式和嵌套类等模式
                    patterns_to_check = {
                        k: v
                        for k, v in self.class_patterns.items()
                        if k
                        in [
                            "nested_class",
                            "nested_struct",
                            "nested_enum",
                            "nested_union",
                        ]
                    }
                    patterns_to_check.update(self.function_body_patterns)
                elif current_state_for_match == ParserState.GLOBAL:
                    patterns_to_check.update(self.global_patterns)
                    patterns_to_check.update(self.out_of_class_patterns)
                elif current_state_for_match == ParserState.NAMESPACE:
                    patterns_to_check.update(self.global_patterns)
                    patterns_to_check.update(self.out_of_class_patterns)
                elif current_state_for_match == ParserState.CLASS:
                    patterns_to_check.update(self.class_patterns)

                # 使用移除字符串后的内容进行模式匹配
                match_found = False
                for pattern_type, pattern in patterns_to_check.items():
                    # --- 新增：仅在函数定义状态下匹配函数调用时移除字符串 ---
                    content_to_search = combined_content  # 默认使用原始合并内容
                    if (
                        current_state_for_match == ParserState.FUNCTION_DEF
                        and pattern_type == "function_call"
                    ):
                        content_to_search = self._strip_string_literals(
                            combined_content
                        )
                        logging.debug(
                            f"Stripped content for function call match (multiline): '{content_to_search}'"
                        )
                    # --- 结束新增 ---

                    match = pattern.search(content_to_search)
                    if match:
                        logging.info(
                            f"MATCHED in multiline content {pattern_type}: '{match.group(0)}' (from {'stripped' if content_to_search != combined_content else 'original'} content)"
                        )
                        # 传递原始 combined_content 给 _process_match (注意：这里的 combined_content 是每行移除 // 注释后的组合)
                        self._process_match(
                            pattern_type, match, line_num, file_path, combined_content
                        )
                        match_found = True
                        break
                return
            else:
                # 括号仍未平衡，继续收集后续行
                return

        # --- 状态机处理字符 (使用 stripped_line) ---
        self.state.reset_line_state()
        self.state.start_line_tracking()  # 开始跟踪行位置

        prev_char = None
        for char in stripped_line:  # 使用 stripped_line
            self.state.advance_position()
            self.state.process_character(char, prev_char)
            prev_char = char
        # --- 结束状态机处理 ---

        # 检查是否存在非行首的未闭合括号 (使用 stripped_line)
        if (
            self.state.unclosed_paren_count > 0
            and "(" in stripped_line
            and not stripped_line.lstrip().startswith("(")
        ):
            logging.debug(
                f"Detected non-leading unclosed parentheses in line: '{stripped_line}'"
            )
            self.state.enter_multiline_paren_mode(stripped_line)  # 使用 stripped_line
            return

        # 检查是否在多行注释中 (这部分逻辑不变，因为它处理 /* */)
        if self.state.was_in_multiline_comment_this_line:
            logging.debug(
                "Line was part of a multi-line comment, skipping pattern/pending logic."
            )
            if not self.state.in_multiline_comment and self.state.pending_lines:
                logging.debug("Multi-line comment ended, clearing any pending lines.")
                self.state.pending_lines.clear()
            return

        # 检查是否适合匹配 (这部分逻辑不变)
        if not self.state.is_clean_for_matching():
            logging.debug(
                "Not clean for matching (in comment/string), skipping pattern checks."
            )
            if self.state.pending_lines and (
                stripped_line.endswith(";")
                or stripped_line.endswith("}")  # 使用 stripped_line
            ):
                logging.debug(
                    "Line ends with ';' or '}', clearing pending lines (even in comment/string)."
                )
                self.state.pending_lines.clear()
            return

        match_found = False
        processed_by_multiline = False
        matched_pattern_type = None

        # 处理 pending lines (使用 stripped_line)
        if self.state.pending_lines:
            if stripped_line.startswith("{"):  # 使用 stripped_line
                # 注意：pending_lines 里的内容也是之前处理过的 stripped_line
                combined_content = (
                    " ".join(self.state.pending_lines) + " " + stripped_line
                )
                logging.debug(f"Attempting combined match: '{combined_content}'")

                # ... (combined content 匹配逻辑不变，因为 combined_content 已处理) ...
                definition_pattern_types = [
                    "namespace",
                    "class",
                    "struct",
                    "nested_class",
                    "nested_struct",
                    "global_function_def",
                    "member_function_def",
                    "out_of_class_member_def",
                    "union",
                    "nested_union",
                    "enum",
                    "nested_enum",
                ]
                patterns_to_try = {
                    **self.global_patterns,
                    **self.class_patterns,
                    **self.out_of_class_patterns,
                }
                relevant_patterns = {
                    ptype: pattern
                    for ptype, pattern in patterns_to_try.items()
                    if ptype in definition_pattern_types
                }

                for pattern_type, pattern in relevant_patterns.items():
                    # 注意：这里主要匹配定义，通常不需要移除字符串
                    content_to_search = combined_content
                    match = pattern.search(content_to_search)
                    if match and (
                        match.group(0).rstrip().endswith("{")
                        or match.group(0).rstrip().endswith("};")
                    ):
                        logging.info(
                            f"MATCHED COMBINED {pattern_type}: '{match.group(0)}' (from original content)"
                        )
                        # 传递 combined_content 给 _process_match
                        self._process_match(
                            pattern_type, match, line_num, file_path, combined_content
                        )
                        self.state.pending_lines.clear()
                        match_found = True
                        processed_by_multiline = True
                        matched_pattern_type = pattern_type
                        break

                if not match_found:
                    logging.debug("Combined match failed, clearing pending lines.")
                    self.state.pending_lines.clear()

            elif stripped_line.endswith(";") or stripped_line.endswith(
                "}"
            ):  # 使用 stripped_line
                logging.debug("Line ends with ';' or '}', clearing pending lines.")
                self.state.pending_lines.clear()
            else:
                logging.debug(
                    f"Appending to pending lines: '{stripped_line}'"
                )  # 使用 stripped_line
                self.state.pending_lines.append(stripped_line)
                processed_by_multiline = True  # 标记为已处理，避免后续单行匹配

        # 单行匹配 (使用 stripped_line)
        if not processed_by_multiline:
            patterns_to_check = {}
            current_state_for_match = self.state.current_state
            # ... (选择 patterns_to_check 的逻辑不变) ...
            if current_state_for_match == ParserState.FUNCTION_DEF:
                patterns_to_check = {
                    k: v
                    for k, v in self.class_patterns.items()
                    if k
                    in ["nested_class", "nested_struct", "nested_enum", "nested_union"]
                }
                patterns_to_check.update(self.function_body_patterns)
            elif current_state_for_match == ParserState.GLOBAL:
                patterns_to_check.update(self.global_patterns)
                patterns_to_check.update(self.out_of_class_patterns)
            elif current_state_for_match == ParserState.NAMESPACE:
                patterns_to_check.update(self.global_patterns)
                patterns_to_check.update(self.out_of_class_patterns)
            elif current_state_for_match == ParserState.CLASS:
                patterns_to_check.update(self.class_patterns)

            for pattern_type, pattern in patterns_to_check.items():
                content_to_search = stripped_line  # 默认使用 stripped_line
                if (
                    current_state_for_match == ParserState.FUNCTION_DEF
                    and pattern_type == "function_call"
                ):
                    content_to_search = self._strip_string_literals(
                        stripped_line
                    )  # 对 stripped_line 再移除字符串
                    logging.debug(
                        f"Stripped content for function call match (single): '{content_to_search}'"
                    )

                match = pattern.search(content_to_search)
                if match:
                    logging.info(
                        f"MATCHED {pattern_type}: '{match.group(0)}' (from {'stripped' if content_to_search != stripped_line else 'original'} line)"
                    )
                    # 传递原始 stripped_line 给 _process_match
                    self._process_match(
                        pattern_type, match, line_num, file_path, stripped_line
                    )
                    match_found = True
                    matched_pattern_type = pattern_type
                    # ... (后续逻辑不变) ...
                    if pattern_type in [
                        "namespace",
                        "class",
                        "struct",
                        "union",
                        "enum",
                        "global_function_def",
                        "member_function_def",
                        "out_of_class_member_def",
                    ]:
                        if self.state.pending_lines:
                            logging.debug(
                                "Clearing pending lines after single-line definition match."
                            )
                            self.state.pending_lines.clear()
                    break

            # 处理未匹配和 pending lines (使用 stripped_line)
            if not match_found:
                if (
                    not stripped_line.endswith("{")  # 使用 stripped_line
                    and not stripped_line.endswith(";")  # 使用 stripped_line
                    and not re.fullmatch(
                        r"(public|protected|private)\s*:",
                        stripped_line,  # 使用 stripped_line
                    )
                    and not stripped_line.startswith("#")  # 使用 stripped_line
                ):
                    logging.debug(
                        f"Starting pending lines: '{stripped_line}'"
                    )  # 使用 stripped_line
                    self.state.pending_lines = [stripped_line]
                else:
                    if self.state.pending_lines:
                        logging.debug(
                            "Clearing pending lines as current line ends scope/statement but didn't match."
                        )
                        self.state.pending_lines.clear()

        # 处理内联函数定义 (使用 stripped_line)
        is_inline_func_def_this_line = False
        if (
            match_found
            and not processed_by_multiline
            and matched_pattern_type
            in ["global_function_def", "member_function_def", "out_of_class_member_def"]
            and "{" in stripped_line  # Check stripped_line for braces
            and "}" in stripped_line  # Check stripped_line for braces
            and stripped_line.rfind("}")
            > stripped_line.rfind("{")  # 使用 stripped_line
        ):
            is_inline_func_def_this_line = True

        # ... (内联函数状态弹出逻辑不变) ...
        if (
            is_inline_func_def_this_line
            and self.state.current_state == ParserState.FUNCTION_DEF
        ):
            if len(self.state.brace_stack) == len(self.state.state_stack) - 1:
                logging.info(
                    f"Auto-popping function state after inline function on line {line_num}"
                )
                self.state.pop_state()

    def _process_match(
        self, pattern_type: str, match, line_num: int, file_path: str, line_content: str
    ):
        """Helper function to process a regex match and update state."""
        # 注意：此函数接收的是原始的 line_content，而不是移除字符串后的内容

        # --- Helper to calculate full name based on current state ---
        def calculate_full_name(base_name, symbol_type_for_scope, qualifier=None):
            # ... (implementation unchanged) ...
            components = []
            current_namespace_stack = list(self.state.namespace_stack)
            current_class_stack = list(self.state.class_stack)
            current_namespace_stack = [
                ns for ns in current_namespace_stack if ns != "<anonymous>"
            ]

            if symbol_type_for_scope in (
                "member_function_def",
                "member_function_decl",
                "member_variable",
                "nested_class",
                "nested_struct",
                "nested_enum",
                "nested_union",
            ):
                if qualifier:
                    qualifier_parts = qualifier.split("::")
                    if not qualifier.startswith("::"):
                        components.extend(current_namespace_stack)
                    components.extend(qualifier_parts)
                elif current_class_stack:
                    components.extend(current_namespace_stack)
                    components.extend(current_class_stack)
                else:
                    components.extend(current_namespace_stack)
            elif symbol_type_for_scope in ("class", "struct", "enum", "union"):
                components.extend(current_namespace_stack)
                if len(current_class_stack) > 0:
                    components.extend(current_class_stack[:-1])
            elif symbol_type_for_scope == "namespace":
                if len(current_namespace_stack) > 0:
                    components.extend(current_namespace_stack[:-1])
            else:  # Global functions, variables, macros
                components.extend(current_namespace_stack)

            full_name_parts = components + [base_name]
            full_name_parts = [part for part in full_name_parts if part]
            return "::".join(full_name_parts)

        # --- End Helper ---

        # --- 获取相对路径，用于生成唯一键 ---
        try:
            relative_path = os.path.relpath(file_path)
        except ValueError:
            relative_path = file_path
        # --- 结束 ---

        # 处理匹配结果
        if pattern_type == "namespace":
            # ... (namespace logic remains the same) ...
            ns_name_match = match.group(1)
            ns_name = ns_name_match if ns_name_match else "<anonymous>"
            # 计算完整名称用于记录
            full_ns_name = (
                calculate_full_name(ns_name_match, "namespace")
                if ns_name_match
                else "<anonymous>"
            )
            self.state.push_state(ParserState.NAMESPACE, ns_name)
            if ns_name_match:
                self._record_symbol(
                    "namespace", ns_name_match, line_num, file_path
                )  # 记录时仍用基本名
        elif pattern_type in [
            "class",
            "struct",
            "nested_class",
            "nested_struct",
        ]:
            # ... (class/struct logic remains the same) ...
            class_name = match.group(1)
            actual_type = "struct" if "struct" in pattern_type else "class"
            # 计算完整名称用于状态和记录
            full_class_name = calculate_full_name(class_name, actual_type)
            self.state.push_state(ParserState.CLASS, class_name)  # 状态栈用基本名
            self._record_symbol(
                actual_type, class_name, line_num, file_path
            )  # 记录时用基本名
        elif pattern_type == "access_modifier":
            # ... (access modifier logic remains the same) ...
            self.state.current_access = match.group(1)
        elif pattern_type == "member_function_def":
            func_name = match.group(2)
            # --- 修正顺序 ---
            full_func_name = calculate_full_name(
                func_name, "member_function_def"
            )  # 先计算
            unique_key = f"{relative_path}::{full_func_name}"  # 再使用
            # --- 结束修正 ---
            self._record_symbol("member_function_def", func_name, line_num, file_path)
            self.state.push_state(ParserState.FUNCTION_DEF, unique_key)  # 传递唯一键
        elif pattern_type == "out_of_class_member_def":
            qualifier = match.group(2).rstrip("::")
            func_name = match.group(3)
            # --- 修正顺序 ---
            full_func_name = calculate_full_name(
                func_name, "member_function_def", qualifier=qualifier
            )  # 先计算
            unique_key = f"{relative_path}::{full_func_name}"  # 再使用
            # --- 结束修正 ---
            self._record_symbol(
                "member_function_def",
                func_name,
                line_num,
                file_path,
                qualifier=qualifier,
            )
            self.state.push_state(ParserState.FUNCTION_DEF, unique_key)  # 传递唯一键
        elif pattern_type == "global_function_def":
            func_name = match.group(2)  # Corrected group index based on regex
            qualifier = None
            record_type = "global_function_def"
            if "::" in func_name:
                logging.warning(
                    f"global_function_def matched qualified name '{func_name}', treating as out-of-class."
                )
                parts = func_name.split("::")
                qualifier = "::".join(parts[:-1])
                func_name = parts[-1]  # 更新 func_name 为基本名
                record_type = "member_function_def"  # 记录类型改为成员函数

            # --- 修正顺序 ---
            full_func_name = calculate_full_name(
                func_name, record_type, qualifier=qualifier
            )  # 先计算
            unique_key = f"{relative_path}::{full_func_name}"  # 再使用
            # --- 结束修正 ---
            self._record_symbol(
                record_type, func_name, line_num, file_path, qualifier=qualifier
            )
            self.state.push_state(ParserState.FUNCTION_DEF, unique_key)  # 传递唯一键
        elif pattern_type in ["enum", "nested_enum"]:
            # ... (enum logic remains the same) ...
            enum_name = match.group(1)
            self._record_symbol("enum", enum_name, line_num, file_path)
        elif pattern_type in ["union", "nested_union"]:
            # ... (union logic remains the same) ...
            union_name = match.group(1)
            # 计算完整名称用于状态
            full_union_name = calculate_full_name(union_name, "union")
            self._record_symbol("union", union_name, line_num, file_path)
            self.state.push_state(ParserState.CLASS, union_name)  # 状态栈用基本名
        elif pattern_type == "function_call":
            # ... (function call logic remains the same) ...
            try:
                # 检查整行的上下文，判断是否是变量声明而非函数调用
                # 使用原始的 line_content 进行检查
                full_match_text = match.group(0)
                # 查找位置时也使用原始 line_content
                start_index = line_content.find(full_match_text)

                if start_index == -1:
                    logging.warning(
                        f"Could not locate match '{full_match_text}' in line '{line_content}', using regex match position"
                    )
                    start_index = match.start()

                # 详细记录上下文信息，用于调试
                logging.debug(
                    f"Function call match: '{full_match_text}' at position {start_index} in '{line_content}'"
                )

                # 后处理：检查匹配项前面的上下文，避免误匹配声明类型的情况
                valid_match = True

                # 检查前缀，判断是否是变量声明 (使用原始 line_content)
                if start_index > 0:
                    # 获取匹配前的所有内容
                    prefix = line_content[:start_index].strip()
                    logging.debug(f"Prefix before function call: '{prefix}'")

                    # 特殊处理：如果前缀是return关键字，则始终视为有效函数调用
                    if prefix == "return":
                        logging.debug(
                            f"Detected 'return' keyword before function call - marking as valid function call"
                        )
                        valid_match = True
                    else:
                        # 关键改进：检查前缀是否是单个标识符（变量类型）
                        # 这种情况通常表示 "类型 变量名()" 的声明模式
                        if re.match(r"^[a-zA-Z_]\w*$", prefix):
                            logging.debug(
                                f"Detected simple variable type '{prefix}' before function call, likely a variable declaration"
                            )
                            valid_match = False

                        # 检查更复杂的类型声明模式
                        elif re.search(r"\b[a-zA-Z_]\w*(?:\s*[*&])?\s*$", prefix):
                            logging.debug(
                                f"Detected complex variable type pattern in '{prefix}', likely a variable declaration"
                            )
                            valid_match = False

                        # 检查是否有多个单词，最后一个可能是类型名
                        words_before = re.findall(r"\b[a-zA-Z_]\w*\b", prefix)
                        if len(words_before) >= 1:
                            logging.debug(
                                f"Found potential type identifier '{words_before[-1]}' before function call"
                            )
                            # 如果前缀只是单独的一个标识符，几乎肯定是变量声明
                            # 但要排除"return"关键字的情况
                            if (
                                len(words_before) == 1
                                and prefix == words_before[0]
                                and words_before[0] != "return"
                            ):
                                logging.debug(
                                    f"Single identifier '{prefix}' before function-like pattern, marking as invalid function call"
                                )
                                valid_match = False

                if valid_match:
                    # 捕获组 1 是完整路径+函数名
                    called_func_string = match.group(1)
                    base_func_name = match.group(2) if len(match.groups()) > 1 else None
                    logging.debug(
                        f"Valid function call: '{called_func_string}', base name: '{base_func_name}'"
                    )

                    # 记录函数调用
                    self._record_symbol(
                        "function_call", called_func_string, line_num, file_path
                    )
                else:
                    logging.debug(
                        f"Skipping invalid function call match: '{full_match_text}' - identified as variable declaration"
                    )
            except IndexError:
                logging.error(
                    f"IndexError extracting function call name. Match groups: {match.groups()}"
                )
            except Exception as e:
                logging.error(
                    f"Error processing function call match: {str(e)}", exc_info=True
                )
        else:  # 处理其他声明和简单匹配
            # ... (logic for other types remains the same) ...
            symbol_name = None
            kwargs = {}
            try:
                if pattern_type in ["macro_constant", "macro_function"]:
                    symbol_name = match.group(1)
                    if pattern_type == "macro_constant":
                        value = match.group(2).strip()
                        kwargs = {"value": value}
                elif pattern_type in [
                    "global_function_decl",
                    "member_function_decl",
                    "friend_function_decl",
                ]:
                    return_type = match.group(1).strip() if match.group(1) else ""
                    symbol_name = match.group(2)
                    parameters = match.group(3).strip()

                    if pattern_type == "member_function_decl":
                        # ... (constructor/destructor logic remains the same) ...
                        current_class_name = (
                            self.state.class_stack[-1]
                            if self.state.class_stack
                            else None
                        )
                        if not return_type and symbol_name.startswith("~"):
                            return_type = "<destructor>"
                        elif (
                            not return_type
                            and current_class_name
                            and symbol_name == current_class_name
                        ):
                            return_type = "<constructor>"
                        elif not return_type:
                            return_type = "<unknown>"

                    kwargs = {"return_type": return_type, "parameters": parameters}

                elif pattern_type in ["global_variable", "member_variable"]:
                    var_type = match.group(1).strip()
                    symbol_name = match.group(2)
                    kwargs = {"var_type": var_type}

                if symbol_name:
                    # 对于非函数调用，直接记录
                    self._record_symbol(
                        pattern_type, symbol_name, line_num, file_path, **kwargs
                    )

            except IndexError:
                logging.error(
                    f"IndexError extracting groups for {pattern_type}. Match groups: {match.groups()}. Regex: {self.all_patterns.get(pattern_type, 'N/A')}"  # Safer access
                )
            except Exception as e:
                logging.error(
                    f"Error processing match for {pattern_type} on line {line_num}: {e}. Match groups: {match.groups()}"
                )

    def _record_symbol(
        self, symbol_type: str, name: str, line_num: int, file_path: str, **kwargs
    ):
        """记录找到的符号，并处理函数调用关联"""
        # 添加额外的安全检查，防止记录明显错误的符号
        # Check for keywords mistaken as variable names or types
        keywords = {
            "if",
            "for",
            "while",
            "switch",
            "return",
            "throw",
            "try",
            "catch",
            "class",
            "struct",
            "namespace",
            "enum",
            "union",
            "using",
            "typedef",
            "friend",
            "public",
            "protected",
            "private",
            "static",
            "const",
            "virtual",
            "inline",
            "explicit",
            "extern",
            "mutable",
            "volatile",
            "constexpr",
            "noexcept",
            "sizeof",
            "new",
            "delete",
            "this",
            "true",
            "false",
            "nullptr",
        }
        if name in keywords:
            logging.debug(
                f"Skipping keyword detected as symbol name: '{name}' as {symbol_type}"
            )
            return
        var_type_kw = kwargs.get("var_type")
        if var_type_kw and var_type_kw in keywords:
            logging.debug(
                f"Skipping keyword detected as variable type: '{var_type_kw}' for symbol '{name}'"
            )
            return
        if symbol_type == "global_variable" and name == "return":
            logging.debug(f"Skipping 'return' keyword as global_variable")
            return

        logging.debug(
            f"Recording symbol: type='{symbol_type}', name='{name}', line={line_num}, kwargs={kwargs}"
        )

        # --- 函数调用关联 ---
        # 如果当前记录的是函数调用，并且状态机处于函数定义内部
        if (
            symbol_type == "function_call"
            and self.state.current_defining_function_full_name
        ):
            defining_func = self.state.current_defining_function_full_name
            # 将捕获到的完整调用字符串 'name' 添加到调用图中
            self.call_graph[defining_func].add(name)
            logging.debug(f"Call recorded: '{defining_func}' calls '{name}'")
            # 注意：'name' 现在是 "prefix::name" 或 "obj.name" 或 "ptr->name" 或 "name"

        # --- 计算完整名称 ---
        components = []
        current_namespace_stack = list(self.state.namespace_stack)
        current_class_stack = list(self.state.class_stack)
        current_namespace_stack = [
            ns for ns in current_namespace_stack if ns != "<anonymous>"
        ]
        qualifier = kwargs.get("qualifier")

        if symbol_type in (
            "member_function_def",
            "member_function_decl",
            "member_variable",
            "nested_class",
            "nested_struct",
            "nested_enum",
            "nested_union",
        ):
            if qualifier:
                # Class/struct specified explicitly (out-of-line definition)
                qualifier_parts = qualifier.split("::")
                # If the qualifier starts with '::', it's global, don't prepend namespaces
                if not qualifier.startswith("::"):
                    components.extend(current_namespace_stack)
                components.extend(qualifier_parts)
            elif current_class_stack:
                # Inside a class/struct definition
                components.extend(current_namespace_stack)
                components.extend(current_class_stack)
            else:  # Should not happen for member symbols, but as fallback
                components.extend(current_namespace_stack)
        elif symbol_type in ("class", "struct", "enum", "union"):
            # Top-level class/struct/enum/union within a namespace or global
            components.extend(current_namespace_stack)
            if len(current_class_stack) > 0:
                components.extend(current_class_stack[:-1])
        elif symbol_type == "namespace":
            if len(current_namespace_stack) > 0:
                components.extend(current_namespace_stack[:-1])
        elif symbol_type == "function_call":
            components.extend(current_namespace_stack)
            components.extend(
                current_class_stack
            )  # Calls happen within class scope too
        else:  # Global functions, variables, macros
            components.extend(current_namespace_stack)

        # Construct full name
        full_name_parts = components + [name]
        # Filter out empty strings just in case
        full_name_parts = [part for part in full_name_parts if part]
        full_name = "::".join(full_name_parts)

        logging.info(
            f"Recorded symbol: Full Name='{full_name}', Type='{symbol_type}', Line={line_num}"
        )

        try:
            relative_path = os.path.relpath(file_path)
        except ValueError:
            relative_path = file_path

        location = f"{relative_path}:{line_num}"
        symbol_info = {"location": location}

        # --- 计算完整名称和唯一键 ---
        components = []
        current_namespace_stack = list(self.state.namespace_stack)
        current_class_stack = list(self.state.class_stack)
        current_namespace_stack = [
            ns for ns in current_namespace_stack if ns != "<anonymous>"
        ]
        qualifier = kwargs.get("qualifier")

        # ... (logic for building components based on symbol_type remains the same) ...
        if symbol_type in (
            "member_function_def",
            "member_function_decl",
            "member_variable",
            "nested_class",
            "nested_struct",
            "nested_enum",
            "nested_union",
        ):
            if qualifier:
                qualifier_parts = qualifier.split("::")
                if not qualifier.startswith("::"):
                    components.extend(current_namespace_stack)
                components.extend(qualifier_parts)
            elif current_class_stack:
                components.extend(current_namespace_stack)
                components.extend(current_class_stack)
            else:
                components.extend(current_namespace_stack)
        elif symbol_type in ("class", "struct", "enum", "union"):
            components.extend(current_namespace_stack)
            if len(current_class_stack) > 0:
                components.extend(
                    current_class_stack[:-1]
                )  # Corrected: extend, not append
        elif symbol_type == "namespace":
            if len(current_namespace_stack) > 0:
                components.extend(
                    current_namespace_stack[:-1]
                )  # Corrected: extend, not append
        elif symbol_type == "function_call":
            # For function calls, 'name' is the called string, full_name is not needed for the key here
            pass
        else:  # Global functions, variables, macros
            components.extend(current_namespace_stack)

        # Construct full name (used for display and potentially lookup)
        full_name_parts = components + [name]
        full_name_parts = [part for part in full_name_parts if part]
        full_name = "::".join(full_name_parts)

        # --- 新增：为函数定义创建唯一键 ---
        unique_key = full_name  # Default to full_name for non-function-defs
        if symbol_type in ["global_function_def", "member_function_def"]:
            unique_key = f"{relative_path}::{full_name}"
            symbol_info["unique_key"] = unique_key  # Store unique key for definitions
            logging.debug(f"Generated unique key for function definition: {unique_key}")
        # --- 结束新增 ---

        logging.info(
            f"Recorded symbol: Full Name='{full_name}', Unique Key='{unique_key if 'unique_key' in symbol_info else 'N/A'}', Type='{symbol_type}', Line={line_num}"
        )

        # --- 函数调用关联 ---
        # 使用状态机中存储的唯一键 (defining_func_unique_key)
        if (
            symbol_type == "function_call"
            and self.state.current_defining_function_full_name  # This now holds the unique_key
        ):
            defining_func_unique_key = self.state.current_defining_function_full_name
            # 使用 unique_key 作为 call_graph 的键
            self.call_graph[defining_func_unique_key].add(
                name
            )  # 'name' is the called string
            logging.debug(f"Call recorded: '{defining_func_unique_key}' calls '{name}'")

        # 添加额外信息
        kwargs.pop("qualifier", None)  # 已使用，不需要再保存
        symbol_info.update(kwargs)

        self.symbols[symbol_type][full_name].append(symbol_info)

    def scan_directory(self, dir_path: str):
        """扫描目录下的所有C++文件"""
        logging.info(f"Starting directory scan: {dir_path}")
        self.reset()  # Reset state for the directory scan

        for root, _, files in os.walk(dir_path):
            for file in files:
                if file.endswith((".cpp", ".h", ".hpp", ".cc", ".cxx", ".hh", ".c")):
                    full_path = os.path.join(root, file)
                    self.scan_file(full_path)
        logging.info(f"Finished directory scan: {dir_path}")

    def get_symbols(self) -> Dict[str, Dict[str, List[dict]]]:
        """获取所有找到的符号"""
        return self.symbols

    def print_summary(self):
        """打印格式化且彩色的符号摘要"""
        colorama.init(autoreset=True)

        # 定义类型颜色
        type_colors = {
            "namespace": colorama.Fore.CYAN,
            "class": colorama.Fore.GREEN,
            "struct": colorama.Fore.YELLOW,
            "enum": colorama.Fore.YELLOW,
            "union": colorama.Fore.YELLOW,
            "global_function_decl": colorama.Fore.BLUE,
            "global_function_def": colorama.Fore.LIGHTBLUE_EX,
            "member_function_decl": colorama.Fore.LIGHTGREEN_EX,
            "member_function_def": colorama.Fore.LIGHTWHITE_EX,
            "friend_function_decl": colorama.Fore.LIGHTYELLOW_EX,
            "global_variable": colorama.Fore.MAGENTA,
            "member_variable": colorama.Fore.LIGHTMAGENTA_EX,
            "macro_constant": colorama.Fore.LIGHTCYAN_EX,
            "macro_function": colorama.Fore.LIGHTRED_EX,
            "function_call": colorama.Fore.RED,
        }
        default_color = colorama.Style.RESET_ALL

        # 创建类型分类映射，将相似类型合并
        type_categories = {
            "struct": "DATA_STRUCTURES",
            "union": "DATA_STRUCTURES",
            "enum": "DATA_STRUCTURES",
            "function_call": "FUNCTION_CALLS",
        }

        # 根据分类组织符号
        categorized_symbols = defaultdict(lambda: defaultdict(list))
        for symbol_type, symbols_by_name in self.symbols.items():
            # 确定该类型的分类
            category = type_categories.get(symbol_type, symbol_type.upper())
            # 将该类型下的所有符号添加到对应分类中，并记录原始类型
            for name, occurrences in symbols_by_name.items():
                for occurrence in occurrences:
                    occurrence_copy = occurrence.copy()
                    occurrence_copy["original_type"] = symbol_type
                    occurrence_copy["full_name"] = name
                    categorized_symbols[category][name].append(occurrence_copy)

        logging.info("Starting summary print.")
        if not self.symbols:
            print("未找到任何符号。")
            logging.info("No symbols found to print.")
            return

        # 打印分类摘要
        for category, symbols_by_name in sorted(categorized_symbols.items()):
            # 对于合并的类型，显示特定颜色
            if category == "DATA_STRUCTURES":
                color = colorama.Fore.YELLOW
                print(f"\n{color}=== 数据结构 (STRUCT/UNION/ENUM) ===")
                logging.debug(f"Printing summary for category: {category}")
            elif category == "FUNCTION_CALLS":
                continue  # Skip printing raw function calls for now
            else:
                # 查找原始类型的颜色
                original_type = next(iter(symbols_by_name.values()))[0].get(
                    "original_type", category
                )
                color = type_colors.get(original_type.lower(), default_color)

                # 根据类型调整显示名称
                category_display = category.lower()
                if category_display == "namespace":
                    category_display = "命名空间"
                elif category_display == "class":
                    category_display = "类"
                elif category_display == "global_function_def":
                    category_display = "全局函数定义"
                elif category_display == "global_function_decl":
                    category_display = "全局函数声明"
                elif category_display == "member_function_def":
                    category_display = "成员函数定义"
                elif category_display == "member_function_decl":
                    category_display = "成员函数声明"
                elif category_display == "global_variable":
                    category_display = "全局变量"
                elif category_display == "member_variable":
                    category_display = "成员变量"
                else:
                    category_display = category

                print(f"\n{color}=== {category_display.upper()} ===")
                logging.debug(f"Printing summary for type: {original_type}")

            if not symbols_by_name:
                print(f"  (无)")
                logging.debug(f"  No symbols of category {category} found.")
                continue

            for full_name, symbol_occurrences in sorted(symbols_by_name.items()):
                original_type = symbol_occurrences[0].get("original_type", "")
                display_name = full_name

                # 对于数据结构类别，显示具体类型
                if category == "DATA_STRUCTURES":
                    print(f"{color}{display_name} ({original_type}){default_color}")
                    logging.debug(f"  Symbol: {display_name} ({original_type})")
                else:
                    print(f"{color}{display_name}{default_color}")
                    logging.debug(f"  Symbol: {display_name}")

                sorted_occurrences = sorted(
                    symbol_occurrences,
                    key=lambda x: (
                        x["location"].split(":")[0],
                        int(x["location"].split(":")[1]),
                    ),
                )

                for info in sorted_occurrences:
                    loc = info["location"]
                    extra_info = ""
                    # current_full_name = info.get("full_name", "") # Keep this for potential use
                    unique_key = info.get("unique_key")  # Get the stored unique key

                    # 添加调用列表到函数定义
                    if original_type in ["global_function_def", "member_function_def"]:
                        if (
                            unique_key and unique_key in self.call_graph
                        ):  # Check if unique_key exists and is in call_graph
                            calls = sorted(list(self.call_graph[unique_key]))
                            if calls:
                                calls_str = ", ".join(calls)
                                extra_info += f" {colorama.Fore.LIGHTBLACK_EX}调用:{colorama.Fore.RED} [{calls_str}]{default_color}"
                                logging.debug(
                                    f"    Calls for {unique_key}: {calls_str}"
                                )

                    if original_type == "macro_constant" and "value" in info:
                        extra_info += f" {colorama.Fore.LIGHTBLACK_EX}值:{default_color} {info['value']}"
                        logging.debug(f"    Location: {loc}, Value: {info['value']}")
                    elif (
                        original_type in ["global_variable", "member_variable"]
                        and "var_type" in info
                    ):
                        extra_info += f" {colorama.Fore.LIGHTBLACK_EX}类型:{default_color} {info['var_type']}"
                        logging.debug(f"    Location: {loc}, Type: {info['var_type']}")
                    elif (
                        original_type
                        in [
                            "global_function_decl",
                            "member_function_decl",
                            "friend_function_decl",
                        ]
                        and "return_type" in info
                        and "parameters" in info
                    ):
                        rt = info["return_type"]
                        params = info["parameters"]
                        if rt == "<constructor>" or rt == "<destructor>":
                            extra_info += f" {colorama.Fore.LIGHTBLACK_EX}类型:{default_color} {rt} {colorama.Fore.LIGHTBLACK_EX}参数:{default_color} {params}"
                        else:
                            extra_info += f" {colorama.Fore.LIGHTBLACK_EX}返回:{default_color} {rt} {colorama.Fore.LIGHTBLACK_EX}参数:{default_color} {params}"
                        logging.debug(
                            f"    Location: {loc}, Return: {rt}, Params: {params}"
                        )
                    else:
                        if not extra_info:
                            logging.debug(f"    Location: {loc}")

                    try:
                        file_part, line_part = loc.rsplit(":", 1)
                        print(
                            f"  - {colorama.Fore.LIGHTBLACK_EX}位置:{default_color} {file_part}:{colorama.Fore.LIGHTYELLOW_EX}{line_part}{default_color}{extra_info}"
                        )
                    except ValueError:
                        print(
                            f"  - {colorama.Fore.LIGHTBLACK_EX}位置:{default_color} {loc}{default_color}{extra_info}"
                        )
        logging.info("Finished summary print.")

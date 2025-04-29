import os
import re
from typing import List, Dict, Tuple
from collections import defaultdict
import colorama  # Import colorama
import logging  # Import logging

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


class ParserState:
    """状态机类用于跟踪解析状态和作用域"""

    # 状态常量定义
    GLOBAL = "GLOBAL"
    NAMESPACE = "NAMESPACE"
    CLASS = "CLASS"
    FUNCTION_DEF = "FUNCTION_DEF"
    TEMPLATE = "TEMPLATE"

    def __init__(self):
        # 当前状态
        self.current_state = self.GLOBAL
        # 状态栈用于跟踪嵌套结构
        self.state_stack = []
        # 作用域栈用于跟踪命名空间和类
        self.namespace_stack = []
        self.class_stack = []
        # 括号栈用于正确匹配
        self.brace_stack = []  # 跟踪大括号 { }
        self.paren_stack = []  # 跟踪小括号 ( )
        self.angle_stack = []  # 跟踪尖括号 < > (用于模板)
        # 跟踪字符串和注释状态，避免在字符串或注释中匹配
        self.in_string = False
        self.in_char = False
        self.in_comment = False
        self.in_multiline_comment = False
        self.was_in_multiline_comment_this_line = False  # 新增标志位
        # 上一次处理的不完整内容（处理跨行结构）
        self.pending_lines = []  # 用于累积跨行定义
        # 当前访问修饰符 (public/protected/private)
        self.current_access = "private"  # C++ 默认为 private
        # 预处理器状态
        self.in_preprocessor = False

    def push_state(self, new_state, scope_name=None):
        """进入新状态"""
        self.state_stack.append(self.current_state)
        self.current_state = new_state
        logging.debug(f"State pushed: {new_state}, scope: {scope_name}")

        # 更新作用域信息
        if new_state == self.NAMESPACE and scope_name:
            self.namespace_stack.append(scope_name)
        elif new_state == self.CLASS and scope_name:
            self.class_stack.append(scope_name)

    def pop_state(self):
        """退出当前状态"""
        if not self.state_stack:
            # 防止状态栈为空时出错
            self.current_state = self.GLOBAL
            return False

        old_state = self.current_state
        self.current_state = self.state_stack.pop()

        # 更新作用域信息
        if old_state == self.NAMESPACE and self.namespace_stack:
            popped = self.namespace_stack.pop()
            logging.debug(f"Popped namespace: {popped}")
        elif old_state == self.CLASS and self.class_stack:
            popped = self.class_stack.pop()
            logging.debug(f"Popped class: {popped}")

        logging.debug(f"State popped: from {old_state} back to {self.current_state}")
        return True

    def process_character(self, char, prev_char=None):
        """处理单个字符，更新括号栈和字符串/注释状态"""
        # 标记当前是否处于多行注释状态
        if self.in_multiline_comment:
            self.was_in_multiline_comment_this_line = True

        # 处理注释
        if not self.in_string and not self.in_char:
            if self.in_multiline_comment:
                if prev_char == "*" and char == "/":
                    self.in_multiline_comment = False
                    return  # 结束多行注释后直接返回
                return  # 仍在多行注释中

            if not self.in_comment:
                if prev_char == "/" and char == "/":
                    self.in_comment = True
                    return
                if prev_char == "/" and char == "*":
                    self.in_multiline_comment = True
                    return

        # 在注释中忽略所有内容
        if self.in_comment or self.in_multiline_comment:
            return

        # 处理字符串和字符字面量
        if char == '"' and prev_char != "\\":
            self.in_string = not self.in_string
            return
        if char == "'" and prev_char != "\\":
            self.in_char = not self.in_char
            return

        # 在字符串或字符字面量中忽略括号
        if self.in_string or self.in_char:
            return

        # 处理括号
        if char == "{":
            self.brace_stack.append("{")
        elif char == "}":
            if self.brace_stack:
                self.brace_stack.pop()
                # 检查是否退出了当前作用域
                if not self.brace_stack:
                    # 所有大括号都已匹配，回到全局状态
                    self.pop_state()
                elif len(self.brace_stack) < len(self.state_stack):
                    # 如果括号栈小于状态栈，可能需要退出当前状态
                    # 例如：从函数定义退回到类定义
                    self.pop_state()
                    # 添加此检查以处理嵌套函数定义完成的情况
                    if self.current_state == self.FUNCTION_DEF and len(
                        self.brace_stack
                    ) <= len(self.state_stack):
                        self.pop_state()

        # 小括号跟踪
        elif char == "(":
            self.paren_stack.append("(")
        elif char == ")":
            if self.paren_stack:
                self.paren_stack.pop()

        # 尖括号跟踪 (用于模板)
        elif char == "<":
            if (
                prev_char is not None  # Add this check
                and prev_char
                in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789"
            ):
                # 可能是模板的开始
                self.angle_stack.append("<")
        elif char == ">":
            if self.angle_stack:
                self.angle_stack.pop()

    def reset_line_state(self):
        """在处理新行时重置行内状态"""
        self.in_comment = False
        self.was_in_multiline_comment_this_line = False  # 重置标志位
        # 注意：不重置多行注释状态，因为它可能跨行

    def is_clean_for_matching(self):
        """检查当前状态是否适合进行正则匹配"""
        return not (
            self.in_string
            or self.in_char
            or self.in_comment
            or self.in_multiline_comment
        )

    def get_current_scope(self):
        """获取当前的完整作用域"""
        scope = []
        scope.extend(self.namespace_stack)
        scope.extend(self.class_stack)
        return scope

    def reset(self):
        """重置状态机"""
        self.__init__()


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
                # Avoid matching lines starting with #define
                r"^(?!\s*#\s*define)"
                r"(?:template\s*<[^>]+>\s*)?"  # 模板前缀
                r"(?:(?:inline|constexpr|static|virtual|explicit)\s+)*"  # 修饰符
                # More robust return type
                r"((?:[\w:]+)(?:\s*\*+\s*|\s*&\s*|\s+))+\s*"
                # Function name (allow scope resolution) - Group 2
                r"([a-zA-Z_][a-zA-Z0-9_:]*)\s*"
                # Add negative lookahead for keywords before parenthesis
                r"(?!\s*(?:if|for|while|switch)\b)"
                # 函数名和参数, noexcept
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

        # 组合所有模式供按需选择
        self.all_patterns = {}
        for pattern_dict in [
            self.global_patterns,
            self.class_patterns,
            self.out_of_class_patterns,
        ]:
            self.all_patterns.update(pattern_dict)

        # 其他扫描器状态
        self.symbols = defaultdict(lambda: defaultdict(list))

    def reset(self):
        """重置扫描器状态"""
        logging.info("Resetting scanner state.")
        self.state.reset()
        self.symbols = defaultdict(lambda: defaultdict(list))

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

    def _process_line(self, line: str, line_num: int, file_path: str):
        """处理单行代码，基于状态机逻辑"""
        stripped_line = line.strip()
        logging.debug(f"--- Processing Line {line_num} ({file_path}) ---")
        logging.debug(f"Raw Stripped: '{stripped_line}'")
        logging.debug(
            f"State: Current={self.state.current_state}, NS={self.state.namespace_stack}, Class={self.state.class_stack}, Braces={len(self.state.brace_stack)}, Pending Lines={len(self.state.pending_lines)}"
        )

        if not stripped_line:
            logging.debug("Line is empty, skipping.")
            if self.state.pending_lines:
                logging.debug("Empty line encountered, clearing pending lines.")
                self.state.pending_lines.clear()
            return

        self.state.reset_line_state()
        prev_char = None
        for char in stripped_line:
            self.state.process_character(char, prev_char)
            prev_char = char

        # 检查是否在多行注释中
        if self.state.was_in_multiline_comment_this_line:
            logging.debug(
                "Line was part of a multi-line comment, skipping pattern/pending logic."
            )
            if not self.state.in_multiline_comment and self.state.pending_lines:
                logging.debug("Multi-line comment ended, clearing any pending lines.")
                self.state.pending_lines.clear()
            return

        if not self.state.is_clean_for_matching():
            logging.debug(
                "Not clean for matching (in comment/string), skipping pattern checks."
            )
            if self.state.pending_lines and (
                stripped_line.endswith(";") or stripped_line.endswith("}")
            ):
                logging.debug(
                    "Line ends with ';' or '}', clearing pending lines (even in comment/string)."
                )
                self.state.pending_lines.clear()
            return

        match_found = False
        processed_by_multiline = False
        matched_pattern_type = None

        if self.state.pending_lines:
            if stripped_line.startswith("{"):
                combined_content = (
                    " ".join(self.state.pending_lines) + " " + stripped_line
                )
                logging.debug(f"Attempting combined match: '{combined_content}'")

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
                    match = pattern.search(combined_content)
                    if match and (
                        match.group(0).rstrip().endswith("{")
                        or match.group(0).rstrip().endswith("};")
                    ):
                        logging.info(
                            f"MATCHED COMBINED {pattern_type}: '{match.group(0)}'"
                        )
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

            elif stripped_line.endswith(";") or stripped_line.endswith("}"):
                logging.debug("Line ends with ';' or '}', clearing pending lines.")
                self.state.pending_lines.clear()
            else:
                logging.debug(f"Appending to pending lines: '{stripped_line}'")
                self.state.pending_lines.append(stripped_line)
                processed_by_multiline = True

        if not processed_by_multiline:
            patterns_to_check = {}
            current_state_for_match = self.state.current_state
            if current_state_for_match == ParserState.FUNCTION_DEF:
                patterns_to_check = {
                    k: v
                    for k, v in self.class_patterns.items()
                    if k
                    in ["nested_class", "nested_struct", "nested_enum", "nested_union"]
                }
            elif current_state_for_match == ParserState.GLOBAL:
                patterns_to_check.update(self.global_patterns)
                patterns_to_check.update(self.out_of_class_patterns)
            elif current_state_for_match == ParserState.NAMESPACE:
                patterns_to_check.update(self.global_patterns)
                patterns_to_check.update(self.out_of_class_patterns)
            elif current_state_for_match == ParserState.CLASS:
                patterns_to_check.update(self.class_patterns)

            for pattern_type, pattern in patterns_to_check.items():
                match = pattern.search(stripped_line)
                if match:
                    logging.info(f"MATCHED {pattern_type}: '{match.group(0)}'")
                    self._process_match(
                        pattern_type, match, line_num, file_path, stripped_line
                    )
                    match_found = True
                    matched_pattern_type = pattern_type
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

            if not match_found:
                if (
                    not stripped_line.endswith("{")
                    and not stripped_line.endswith(";")
                    and not re.fullmatch(
                        r"(public|protected|private)\s*:", stripped_line
                    )
                    and not stripped_line.startswith("#")
                ):
                    logging.debug(f"Starting pending lines: '{stripped_line}'")
                    self.state.pending_lines = [stripped_line]
                else:
                    if self.state.pending_lines:
                        logging.debug(
                            "Clearing pending lines as current line ends scope/statement but didn't match."
                        )
                        self.state.pending_lines.clear()

        is_inline_func_def_this_line = False
        if (
            match_found
            and not processed_by_multiline
            and matched_pattern_type
            in ["global_function_def", "member_function_def", "out_of_class_member_def"]
            and "{" in stripped_line
            and "}" in stripped_line
            and stripped_line.rfind("}") > stripped_line.rfind("{")
        ):
            is_inline_func_def_this_line = True

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
        # (Extracted logic from the original loop for clarity)

        # 检查是否在函数体内 (除非是允许的嵌套结构)
        if (
            self.state.current_state == ParserState.FUNCTION_DEF
            and pattern_type
            not in [
                "nested_class",
                "nested_struct",
                "nested_enum",
                "nested_union",
            ]
        ):
            logging.debug(f"Skipping match in function body: {match.group(0)}")
            return

        # 处理匹配结果
        if pattern_type == "namespace":
            ns_name_match = match.group(1)
            ns_name = ns_name_match if ns_name_match else "<anonymous>"
            self.state.push_state(ParserState.NAMESPACE, ns_name)
            if ns_name_match:
                self._record_symbol("namespace", ns_name_match, line_num, file_path)
        elif pattern_type in [
            "class",
            "struct",
            "nested_class",
            "nested_struct",
        ]:
            class_name = match.group(1)
            actual_type = "struct" if "struct" in pattern_type else "class"
            self.state.push_state(ParserState.CLASS, class_name)
            self._record_symbol(actual_type, class_name, line_num, file_path)
        elif pattern_type == "access_modifier":
            self.state.current_access = match.group(1)
        elif pattern_type == "member_function_def":
            func_name = match.group(2)
            self._record_symbol("member_function_def", func_name, line_num, file_path)
            self.state.push_state(ParserState.FUNCTION_DEF)
        elif pattern_type == "out_of_class_member_def":
            qualifier = match.group(2).rstrip(":")
            func_name = match.group(3)
            self._record_symbol(
                "member_function_def",  # Record as member_function_def
                func_name,
                line_num,
                file_path,
                qualifier=qualifier,
            )
            self.state.push_state(ParserState.FUNCTION_DEF)
        elif pattern_type == "global_function_def":
            func_name = match.group(2)  # Check group index
            if "::" in func_name:
                logging.warning(
                    f"global_function_def matched qualified name '{func_name}', treating as out-of-class."
                )
                parts = func_name.split("::")
                class_name = "::".join(parts[:-1])
                method_name = parts[-1]
                self._record_symbol(
                    "member_function_def",  # Record as member_function_def
                    method_name,
                    line_num,
                    file_path,
                    qualifier=class_name,
                )
            else:
                self._record_symbol(
                    "global_function_def", func_name, line_num, file_path
                )
            self.state.push_state(ParserState.FUNCTION_DEF)
        elif pattern_type in ["enum", "nested_enum"]:
            enum_name = match.group(1)
            self._record_symbol("enum", enum_name, line_num, file_path)
        elif pattern_type in ["union", "nested_union"]:
            union_name = match.group(1)
            self._record_symbol("union", union_name, line_num, file_path)
            self.state.push_state(ParserState.CLASS, union_name)
        else:
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
                    self._record_symbol(
                        pattern_type, symbol_name, line_num, file_path, **kwargs
                    )
                else:
                    logging.warning(
                        f"Could not extract symbol name for {pattern_type} from match: {match.groups()}"
                    )

            except IndexError:
                logging.error(
                    f"IndexError extracting groups for {pattern_type}. Match groups: {match.groups()}. Regex: {self.all_patterns[pattern_type].pattern}"
                )

    def _record_symbol(
        self, symbol_type: str, name: str, line_num: int, file_path: str, **kwargs
    ):
        """记录找到的符号，基于当前状态机环境"""
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
        if symbol_type == "global_function_decl" and name.startswith("std::"):
            logging.debug(f"Skipping standard library usage: '{name}' as {symbol_type}")
            return

        logging.debug(
            f"Recording symbol: type='{symbol_type}', name='{name}', line={line_num}, kwargs={kwargs}"
        )
        components = []

        # 基于当前状态机状态构建作用域
        current_namespace_stack = list(self.state.namespace_stack)  # Make copies
        current_class_stack = list(self.state.class_stack)

        current_namespace_stack = [
            ns for ns in current_namespace_stack if ns != "<anonymous>"
        ]

        qualifier = kwargs.get("qualifier")

        if symbol_type in (
            "member_function_def",
            "member_function_decl",
            "member_variable",
            "nested_class",  # Nested types belong to the parent class/struct
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
        }
        default_color = colorama.Style.RESET_ALL

        # 创建类型分类映射，将相似类型合并
        type_categories = {
            "struct": "DATA_STRUCTURES",
            "union": "DATA_STRUCTURES",
            "enum": "DATA_STRUCTURES",
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
                    categorized_symbols[category][name].append(occurrence_copy)

        logging.info("Starting summary print.")
        if not self.symbols:
            print("未找到任何符号。")
            logging.info("No symbols found to print.")
            return

        # 打印分类摘要
        for category, symbols_by_name in sorted(categorized_symbols.items()):
            # 对于合并的类型，使用特定颜色
            if category == "DATA_STRUCTURES":
                color = colorama.Fore.YELLOW
                print(f"\n{color}=== 数据结构 (STRUCT/UNION/ENUM) ===")
                logging.debug(f"Printing summary for category: {category}")
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

            for name, symbol_occurrences in sorted(symbols_by_name.items()):
                # 获取第一个出现的原始类型以显示
                original_type = symbol_occurrences[0].get("original_type", "")

                # 对于数据结构类别，显示具体类型
                if category == "DATA_STRUCTURES":
                    print(f"{color}{name} ({original_type}){default_color}")
                    logging.debug(f"  Symbol: {name} ({original_type})")
                else:
                    print(f"{color}{name}{default_color}")
                    logging.debug(f"  Symbol: {name}")

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

                    original_type = info.get("original_type", "")

                    if original_type == "macro_constant" and "value" in info:
                        extra_info = f" {colorama.Fore.LIGHTBLACK_EX}值:{default_color} {info['value']}"
                        logging.debug(f"    Location: {loc}, Value: {info['value']}")
                    elif (
                        original_type in ["global_variable", "member_variable"]
                        and "var_type" in info
                    ):
                        extra_info = f" {colorama.Fore.LIGHTBLACK_EX}类型:{default_color} {info['var_type']}"
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
                            extra_info = f" {colorama.Fore.LIGHTBLACK_EX}类型:{default_color} {rt} {colorama.Fore.LIGHTBLACK_EX}参数:{default_color} {params}"
                        else:
                            extra_info = f" {colorama.Fore.LIGHTBLACK_EX}返回:{default_color} {rt} {colorama.Fore.LIGHTBLACK_EX}参数:{default_color} {params}"
                        logging.debug(
                            f"    Location: {loc}, Return: {rt}, Params: {params}"
                        )
                    else:
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
    print("\n扫描完成。")
    logging.info("Script execution finished.")

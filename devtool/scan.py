import os
import re
from typing import List, Dict, Tuple
from collections import defaultdict
import colorama  # Import colorama


class CppSymbolScanner:
    def __init__(self):
        # 初始化各种正则表达式模式
        self.patterns = {
            # Match simple macro constants (#define NAME VALUE)
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
            # Match function-like macros (#define NAME(...) ...)
            "macro_function": re.compile(
                r"^\s*#\s*define\s+"  # #define keyword
                r"([a-zA-Z_]\w*)"  # Macro name (Group 1)
                r"\("  # Opening parenthesis for arguments
                # Arguments inside parentheses (non-greedy)
                r"[^)]*\)"
                # Optional replacement value (can be complex, capture simply for now)
                r"(.*)"  # Capture the rest (Group 2)
            ),
            "namespace": re.compile(r"namespace\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\{"),
            "class": re.compile(
                # Allow template<...> before class/struct
                r"(?:template\s*<[^>]+>\s*)?"
                r"class\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*(?:final\s*)?(?::\s*[^{]+)?\{"
            ),
            "struct": re.compile(
                # Allow template<...> before class/struct
                r"(?:template\s*<[^>]+>\s*)?"
                r"struct\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*(?::\s*[^{]+)?\{"
            ),
            "function_decl": re.compile(
                # Avoid matching lines starting with #define
                r"^(?!\s*#\s*define)"
                r"(?:template\s*<[^>]+>\s*)?"  # 模板前缀
                r"(?:(?:inline|constexpr|static|virtual|explicit)\s+)*"  # 修饰符
                # More robust return type (allows pointers, refs, scope resolution)
                r"((?:[\w:]+)(?:\s*\*+\s*|\s*&\s*|\s+))+\s*"
                # Function name (allow scope resolution)
                r"([a-zA-Z_][a-zA-Z0-9_:]*)\s*"
                # 函数名和参数, noexcept
                r"\([^;{}]*\)\s*(?:const\s*)?(?:noexcept(?:\([^)]*\))?)?\s*;"
            ),
            "function_def": re.compile(
                # Avoid matching lines starting with #define
                r"^(?!\s*#\s*define)"
                r"(?:template\s*<[^>]+>\s*)?"  # 模板前缀
                r"(?:(?:inline|constexpr|static|virtual|explicit)\s+)*"  # 修饰符
                # More robust return type
                r"((?:[\w:]+)(?:\s*\*+\s*|\s*&\s*|\s+))+\s*"
                # Function name (allow scope resolution)
                r"([a-zA-Z_][a-zA-Z0-9_:]*)\s*"
                # 函数名和参数, noexcept
                r"\([^{}]*\)\s*(?:const\s*)?(?:noexcept(?:\([^)]*\))?)?\s*\{"
            ),
            "global_var": re.compile(
                # Avoid matching lines starting with #define
                r"^(?!\s*#\s*define)"
                r"(?:extern\s+)?"  # extern修饰符
                r"(?:(?:const|static|volatile|mutable)\s+)*"  # 其他修饰符
                # More robust type
                r"((?:[\w:]+)(?:\s*\*+\s*|\s*&\s*|\s+))+\s*"
                # 变量名
                r"([a-zA-Z_][a-zA-Z0-9_]*)\s*"
                # Look for array, assignment, comma, or semicolon, but NOT immediately followed by (
                r"(?:\[.*?\])?\s*(?![ \t]*\()[=;,]"
            ),
        }

        # 用于跟踪当前命名空间
        self.current_namespace = []
        self.symbols = defaultdict(lambda: defaultdict(list))

    def reset(self):
        """重置扫描器状态"""
        self.current_namespace = []
        self.symbols = defaultdict(lambda: defaultdict(list))

    def scan_file(self, file_path: str):
        """扫描单个C++文件"""
        try:
            with open(file_path, "r", encoding="utf-8") as f:
                content = f.read()

            # 预处理：移除注释以减少干扰
            content = self._remove_comments(content)

            # 按行处理以便跟踪位置
            lines = content.split("\n")
            for line_num, line in enumerate(lines, 1):
                self._process_line(line, line_num, file_path)

        except UnicodeDecodeError:
            print(f"警告: 无法解码文件 {file_path}，跳过")
        except Exception as e:
            print(f"处理文件 {file_path} 时出错: {str(e)}")

    def _remove_comments(self, content: str) -> str:
        """移除C++注释"""
        # 移除多行注释 /* ... */
        content = re.sub(r"/\*.*?\*/", "", content, flags=re.DOTALL)
        # 移除单行注释 //
        content = re.sub(r"//.*$", "", content, flags=re.MULTILINE)
        return content

    def _process_line(self, line: str, line_num: int, file_path: str):
        """处理单行代码"""
        stripped_line = line.strip()
        if not stripped_line:  # Skip empty lines early
            return

        # --- Priority 1: Macros ---
        # Use match() for patterns that must start at the beginning of the line
        macro_func_match = self.patterns["macro_function"].match(stripped_line)
        if macro_func_match:
            macro_name = macro_func_match.group(1)
            self._record_symbol("macro_function", macro_name, line_num, file_path)
            return  # Found macro function, stop processing this line

        macro_const_match = self.patterns["macro_constant"].match(stripped_line)
        if macro_const_match:
            macro_name = macro_const_match.group(1)
            macro_value = macro_const_match.group(2).strip()
            self._record_symbol(
                "macro_constant", macro_name, line_num, file_path, value=macro_value
            )
            return  # Found macro constant, stop processing this line

        # --- Priority 2: Namespace / Class / Struct ---
        # Use search() for patterns that can appear anywhere in the line
        namespace_match = self.patterns["namespace"].search(line)
        if namespace_match:
            ns_name = namespace_match.group(1)
            self.current_namespace.append(ns_name)
            self._record_symbol("namespace", ns_name, line_num, file_path)
            # Don't return yet, a class/struct might be on the same line

        class_match = self.patterns["class"].search(line)
        if class_match:
            class_name = class_match.group(1)
            self._record_symbol("class", class_name, line_num, file_path)
            return  # Assuming class def takes priority

        struct_match = self.patterns["struct"].search(line)
        if struct_match:
            struct_name = struct_match.group(1)
            self._record_symbol("struct", struct_name, line_num, file_path)
            return

        # --- Priority 3: Functions / Variables (only if not a macro) ---
        # Use search() as these might be indented
        func_def_match = self.patterns["function_def"].search(line)
        if func_def_match:
            # Adjust group index if needed based on regex changes
            func_name = func_def_match.group(2)
            self._record_symbol("function_def", func_name, line_num, file_path)
            return

        func_decl_match = self.patterns["function_decl"].search(line)
        if func_decl_match:
            # Adjust group index if needed
            func_name = func_decl_match.group(2)
            self._record_symbol("function_decl", func_name, line_num, file_path)
            return

        global_var_match = self.patterns["global_var"].search(line)
        if global_var_match:
            # Adjust group index if needed
            var_name = global_var_match.group(2)
            self._record_symbol("global_var", var_name, line_num, file_path)
            return

        # --- Namespace End Handling (Simplified) ---
        # WARNING: This is very inaccurate without proper brace counting
        if "}" in stripped_line and self.current_namespace:
            if stripped_line.startswith("}"):
                try:
                    self.current_namespace.pop()
                except IndexError:
                    pass

    def _record_symbol(
        self, symbol_type: str, name: str, line_num: int, file_path: str, **kwargs
    ):
        """记录找到的符号, 允许附加信息"""
        full_name = "::".join(self.current_namespace + [name])
        location = f"{file_path}:{line_num}"
        # Store a dictionary containing location and any extra info
        symbol_info = {"location": location}
        symbol_info.update(kwargs)
        self.symbols[symbol_type][full_name].append(symbol_info)

    def scan_directory(self, dir_path: str):
        """扫描目录下的所有C++文件"""
        self.reset()

        for root, _, files in os.walk(dir_path):
            for file in files:
                if file.endswith((".cpp", ".h", ".hpp", ".cc", ".cxx", ".hh", ".c")):
                    full_path = os.path.join(root, file)
                    self.scan_file(full_path)

    def get_symbols(self) -> Dict[str, Dict[str, List[str]]]:
        """获取所有找到的符号"""
        return self.symbols

    def print_summary(self):
        """打印格式化且彩色的符号摘要"""
        colorama.init(autoreset=True)  # Initialize colorama

        # Define colors for different symbol types
        type_colors = {
            "namespace": colorama.Fore.CYAN,
            "class": colorama.Fore.GREEN,
            "struct": colorama.Fore.YELLOW,
            "function_decl": colorama.Fore.BLUE,
            "function_def": colorama.Fore.LIGHTBLUE_EX,
            "global_var": colorama.Fore.MAGENTA,
            "macro_constant": colorama.Fore.LIGHTCYAN_EX,  # Color for macro constants
            "macro_function": colorama.Fore.LIGHTRED_EX,  # Color for function-like macros
        }
        default_color = colorama.Style.RESET_ALL

        if not self.symbols:
            print("未找到任何符号。")
            return

        for symbol_type, symbols_by_name in sorted(self.symbols.items()):
            color = type_colors.get(symbol_type, default_color)
            print(f"\n{color}=== {symbol_type.upper()} ===")

            if not symbols_by_name:
                print(f"  (无)")
                continue

            # Sort symbols by name
            for name, symbol_occurrences in sorted(symbols_by_name.items()):
                print(f"{color}{name}{default_color}")

                # Sort occurrences by location (file then line)
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
                    # Add value display for macro constants
                    if symbol_type == "macro_constant" and "value" in info:
                        extra_info = f" {colorama.Fore.LIGHTBLACK_EX}值:{default_color} {info['value']}"

                    # Extract file and line number
                    try:
                        file_part, line_part = loc.rsplit(":", 1)
                        print(
                            f"  - {colorama.Fore.LIGHTBLACK_EX}位置:{default_color} {file_part}:{colorama.Fore.LIGHTYELLOW_EX}{line_part}{default_color}{extra_info}"
                        )
                    except ValueError:
                        print(
                            f"  - {colorama.Fore.LIGHTBLACK_EX}位置:{default_color} {loc}{default_color}{extra_info}"
                        )  # Fallback if split fails


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("用法: python cpp_scanner.py <目录路径>")
        sys.exit(1)

    scanner = CppSymbolScanner()
    target_dir = sys.argv[1]
    print(f"开始扫描目录: {target_dir}")
    scanner.scan_directory(target_dir)
    scanner.print_summary()
    print("\n扫描完成。")  # Add a final message

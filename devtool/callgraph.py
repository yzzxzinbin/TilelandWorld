import logging
from collections import defaultdict
import colorama  # 导入 colorama

# 初始化 colorama
colorama.init(autoreset=True)


class CallGraphAnalyzer:
    """分析由 CppSymbolScanner 生成的符号和调用图"""

    def __init__(self, symbols, call_graph):
        """
        初始化分析器。

        Args:
            symbols (dict): 由 CppSymbolScanner 生成的符号字典。
            call_graph (dict): 由 CppSymbolScanner 生成的调用图字典
                               {defining_func_full_name: {called_func_str1, ...}}。
        """
        self.symbols = symbols
        self.call_graph = call_graph
        # Store defined function names for quick lookup in resolve_call
        self.defined_function_names = set(
            self.symbols.get("global_function_def", {}).keys()
        ) | set(self.symbols.get("member_function_def", {}).keys())
        self.symbol_lookup = self.build_symbol_lookup()
        logging.info("CallGraphAnalyzer initialized.")

    def build_symbol_lookup(self):
        """
        构建一个从完整名称到符号详细信息（位置、类型）的查找表。
        优先选择函数定义。
        """
        lookup = {}
        # 优先处理函数定义
        for symbol_type in ["global_function_def", "member_function_def"]:
            if symbol_type in self.symbols:
                for name, occurrences in self.symbols[symbol_type].items():
                    if occurrences:
                        # 存储第一次出现的信息（通常足够）
                        info = occurrences[0].copy()
                        info["type"] = symbol_type  # 确保类型信息存在
                        lookup[name] = info
                        logging.debug(
                            f"Symbol lookup added (definition): {name} -> {info}"
                        )

        # 再处理其他符号，如果尚未被定义覆盖
        for symbol_type, symbols_by_name in self.symbols.items():
            if symbol_type not in ["global_function_def", "member_function_def"]:
                for name, occurrences in symbols_by_name.items():
                    if occurrences and name not in lookup:
                        info = occurrences[0].copy()
                        info["type"] = symbol_type
                        lookup[name] = info
                        logging.debug(f"Symbol lookup added (other): {name} -> {info}")
        logging.info(f"Symbol lookup table built with {len(lookup)} entries.")
        return lookup

    def find_main_functions(self):
        """
        查找可能的 main 函数入口点定义及其位置。
        返回一个包含 (full_name, location) 元组的列表。
        """
        main_funcs_with_loc = []
        # 检查 global_function_def
        if "global_function_def" in self.symbols:
            for full_name, occurrences in self.symbols["global_function_def"].items():
                # 检查基本名称是否为 'main'
                if self._get_base_name(full_name) == "main":
                    for occurrence in occurrences:
                        location = occurrence.get("location", "[Location Unknown]")
                        main_funcs_with_loc.append((full_name, location))
                        logging.debug(
                            f"Found potential main function (global): {full_name} at {location}"
                        )

        # 检查 member_function_def (处理可能被错误分类的情况)
        if "member_function_def" in self.symbols:
            for full_name, occurrences in self.symbols["member_function_def"].items():
                # 检查基本名称是否为 'main'
                if self._get_base_name(full_name) == "main":
                    for occurrence in occurrences:
                        location = occurrence.get("location", "[Location Unknown]")
                        main_funcs_with_loc.append((full_name, location))
                        logging.debug(
                            f"Found potential main function (member def treated as): {full_name} at {location}"
                        )

        logging.info(
            f"Found {len(main_funcs_with_loc)} potential main function definitions."
        )
        # 按位置排序，方便用户查看
        return sorted(main_funcs_with_loc, key=lambda item: item[1])

    def _get_base_name(self, full_name):
        """辅助函数：从完整名称或调用字符串中提取基本名称。"""
        if "::" in full_name:
            return full_name.split("::")[-1]
        elif "." in full_name:
            return full_name.split(".")[-1]
        elif "->" in full_name:
            return full_name.split("->")[-1]
        return full_name

    def resolve_call(self, calling_function_full_name, called_func_str):
        """
        尝试将调用字符串解析为调用图中定义的函数的完整名称。
        简化逻辑：主要匹配基本函数名。

        Args:
            calling_function_full_name (str): 进行调用的函数的完整名称。
            called_func_str (str): 在调用图中记录的被调用函数字符串 (可能包含前缀)。

        Returns:
            str or None: 如果找到匹配基本名称的已定义函数，则返回其完整名称；否则返回 None。
        """
        base_called_name = self._get_base_name(called_func_str)

        # 优先查找与调用者在同一作用域（或子作用域）的匹配项
        if "::" in calling_function_full_name:
            caller_prefix = "::".join(calling_function_full_name.split("::")[:-1])
            for defined_func_full_name in self.defined_function_names:
                base_defined_name = self._get_base_name(defined_func_full_name)
                if base_called_name == base_defined_name:
                    # 检查是否在同一作用域或子作用域
                    if defined_func_full_name.startswith(caller_prefix):
                        logging.debug(
                            f"Resolved '{called_func_str}' to '{defined_func_full_name}' via base name and scope match."
                        )
                        return defined_func_full_name

        # 如果在同一作用域找不到，则查找全局匹配的基本名称
        for defined_func_full_name in self.defined_function_names:
            base_defined_name = self._get_base_name(defined_func_full_name)
            if base_called_name == base_defined_name:
                logging.debug(
                    f"Resolved '{called_func_str}' to '{defined_func_full_name}' via base name match (fallback)."
                )
                return defined_func_full_name  # 返回第一个找到的匹配项

        logging.debug(
            f"Could not resolve call '{called_func_str}' from '{calling_function_full_name}' by base name."
        )
        return None

    def trace_call_chain(
        self, start_function_full_name, visited=None, depth=0, max_depth=20
    ):
        """
        递归地追踪并打印从指定函数开始的调用链 (带颜色)。

        Args:
            start_function_full_name (str): 起始函数的完整名称。
            visited (set, optional): 用于检测循环的已访问函数集合。默认为 None。
            depth (int, optional): 当前递归深度，用于缩进。默认为 0。
            max_depth (int, optional): 最大递归深度，防止无限循环。默认为 20。
        """
        indent = "  " * depth
        location_info = self.symbol_lookup.get(start_function_full_name, {}).get(
            "location", "[Location Unknown]"
        )
        # 函数名用亮白色，位置用灰色
        print(
            f"{indent}{colorama.Fore.WHITE}- {start_function_full_name} "
            f"{colorama.Fore.LIGHTBLACK_EX}({location_info}){colorama.Style.RESET_ALL}"
        )

        if depth >= max_depth:
            # 最大深度提示用黄色
            print(
                f"{indent}  {colorama.Fore.YELLOW}[Max depth reached]{colorama.Style.RESET_ALL}"
            )
            return

        if visited is None:
            visited = set()

        # 检测循环
        if start_function_full_name in visited:
            # 循环提示用黄色
            print(
                f"{indent}  {colorama.Fore.YELLOW}[Cycle detected]{colorama.Style.RESET_ALL}"
            )
            return

        visited.add(start_function_full_name)

        # 获取此函数调用的其他函数
        called_functions = self.call_graph.get(start_function_full_name, set())

        if not called_functions:
            pass  # Or print a grey message: print(f"{indent}  {colorama.Fore.LIGHTBLACK_EX}[Leaf function]{colorama.Style.RESET_ALL}")

        sorted_called_functions = sorted(list(called_functions))

        for called_func_str in sorted_called_functions:
            # 尝试解析调用
            resolved_callee_full_name = self.resolve_call(
                start_function_full_name, called_func_str
            )

            if resolved_callee_full_name:
                # 递归追踪解析出的函数
                self.trace_call_chain(
                    resolved_callee_full_name, visited.copy(), depth + 1, max_depth
                )
            else:
                # 未解析调用用红色
                print(
                    f"{indent}  {colorama.Fore.RED}-> {called_func_str} [External/Unresolved/Member Call]{colorama.Style.RESET_ALL}"
                )

    def find_uncalled_functions(self):
        """
        查找所有已定义但从未在解析的调用图中被调用的函数。
        """
        defined_functions = self.defined_function_names  # 使用初始化时存储的集合
        resolved_called_functions = set()

        logging.debug("Starting search for uncalled functions...")
        logging.debug(f"Total defined functions: {len(defined_functions)}")

        # 遍历调用图，解析所有调用
        for defining_func, called_set in self.call_graph.items():
            for called_func_str in called_set:
                resolved_callee = self.resolve_call(defining_func, called_func_str)
                if resolved_callee:
                    if resolved_callee not in resolved_called_functions:
                        logging.debug(
                            f"Adding '{resolved_callee}' to resolved called functions set."
                        )
                        resolved_called_functions.add(resolved_callee)

        logging.debug(
            f"Total resolved called functions: {len(resolved_called_functions)}"
        )

        # 计算差集
        uncalled_functions = defined_functions - resolved_called_functions
        logging.info(f"Found {len(uncalled_functions)} uncalled functions.")

        return sorted(list(uncalled_functions))

    def interactive_trace(self):
        """提供一个交互式菜单，让用户选择 main 函数的具体定义位置并开始追踪，最后列出未调用函数 (带颜色)。"""
        # 标题用青色
        print(
            f"\n{colorama.Fore.CYAN}--- Call Chain Tracer ---{colorama.Style.RESET_ALL}"
        )
        # 获取 main 函数及其位置列表
        main_definitions = self.find_main_functions()
        selected_main_full_name = None  # 用于存储最终选择的函数名

        if not main_definitions:
            print("No 'main' function definition found to start tracing.")
        elif len(main_definitions) == 1:
            selected_main_full_name, location = main_definitions[0]
            # 提示信息用绿色
            print(
                f"{colorama.Fore.GREEN}Found one main function definition: {colorama.Fore.WHITE}{selected_main_full_name} "
                f"{colorama.Fore.LIGHTBLACK_EX}({location}){colorama.Style.RESET_ALL}"
            )
        else:  # 找到多个 main 函数定义位置
            print("Multiple 'main' function definitions found:")
            for i, (full_name, location) in enumerate(main_definitions):
                # 选项用默认颜色，函数名用白色，位置用灰色
                print(
                    f"  {i + 1}: {colorama.Fore.WHITE}{full_name} {colorama.Fore.LIGHTBLACK_EX}({location}){colorama.Style.RESET_ALL}"
                )

            # 使用 while True 循环，直到获得有效选择或用户选择跳过
            while True:
                try:
                    # 提示输入用黄色
                    choice = input(
                        f"{colorama.Fore.YELLOW}Select a main function definition to trace (1-{len(main_definitions)}), or 0 to skip tracing: {colorama.Style.RESET_ALL}"
                    )
                    index = int(choice) - 1
                    if index == -1:  # 用户选择 0 跳过
                        selected_main_full_name = None  # 确保为 None
                        break  # 退出选择循环
                    if 0 <= index < len(main_definitions):
                        # 获取选定位置对应的函数名
                        selected_main_full_name, _ = main_definitions[index]
                        break  # 退出选择循环
                    else:
                        # 错误提示用红色
                        print(
                            f"{colorama.Fore.RED}Invalid choice.{colorama.Style.RESET_ALL}"
                        )
                except ValueError:
                    # 错误提示用红色
                    print(
                        f"{colorama.Fore.RED}Invalid input. Please enter a number.{colorama.Style.RESET_ALL}"
                    )
                # 如果输入无效或选择超出范围，循环会继续

        # --- 执行追踪 ---
        if selected_main_full_name:
            # 使用选择的函数名开始追踪
            print(
                f"\nTracing call chain starting from: {colorama.Fore.WHITE}{selected_main_full_name}{colorama.Style.RESET_ALL}\n"
            )
            self.trace_call_chain(selected_main_full_name)
            print(
                f"\n{colorama.Fore.CYAN}--- End Call Chain Trace ---{colorama.Style.RESET_ALL}"
            )
        else:
            # 只有在找到了 main 函数定义但用户选择跳过时才打印此消息
            if main_definitions:
                print("\nSkipping call chain tracing.")

        # --- 列出未调用函数 (这部分逻辑总会执行) ---
        print(
            f"\n{colorama.Fore.CYAN}--- Uncalled Functions ---{colorama.Style.RESET_ALL}"
        )
        uncalled = self.find_uncalled_functions()
        if uncalled:
            print("The following defined functions appear to be uncalled:")
            for func_name in uncalled:
                location_info = self.symbol_lookup.get(func_name, {}).get(
                    "location", "[Location Unknown]"
                )
                # 函数名用黄色，位置用灰色
                print(
                    f"- {colorama.Fore.YELLOW}{func_name} {colorama.Fore.LIGHTBLACK_EX}({location_info}){colorama.Style.RESET_ALL}"
                )
        else:
            # 成功信息用绿色
            print(
                f"{colorama.Fore.GREEN}All defined functions appear to be called within the analyzed scope.{colorama.Style.RESET_ALL}"
            )
        print(
            f"{colorama.Fore.CYAN}--- End Uncalled Functions ---{colorama.Style.RESET_ALL}"
        )

import logging
from collections import defaultdict
import colorama  # 导入 colorama
import os

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
        self.call_graph = (
            call_graph  # Keys are now unique_keys (e.g., "file.cpp::func")
        )
        # --- 修改：存储 unique_keys ---
        self.defined_function_unique_keys = set()
        for symbol_type in ["global_function_def", "member_function_def"]:
            if symbol_type in self.symbols:
                for name, occurrences in self.symbols[symbol_type].items():
                    for info in occurrences:
                        if "unique_key" in info:
                            self.defined_function_unique_keys.add(info["unique_key"])
        # --- 结束修改 ---
        self.symbol_lookup = self.build_symbol_lookup()  # Needs update
        logging.info("CallGraphAnalyzer initialized.")
        logging.debug(
            f"Defined function unique keys: {self.defined_function_unique_keys}"
        )
        logging.debug(f"Call graph keys: {list(self.call_graph.keys())}")

    def build_symbol_lookup(self):
        """
        构建一个从唯一键到符号详细信息（位置、类型）的查找表。
        """
        lookup = {}
        for symbol_type, symbols_by_name in self.symbols.items():
            for name, occurrences in symbols_by_name.items():
                for info in occurrences:
                    # --- 修改：使用 unique_key 作为主键 ---
                    unique_key = info.get("unique_key")
                    if unique_key:  # Primarily index by unique_key if available
                        if (
                            unique_key not in lookup
                        ):  # Prioritize first occurrence (often definition)
                            info_copy = info.copy()
                            info_copy["type"] = symbol_type
                            info_copy["base_name"] = self._get_base_name(
                                name
                            )  # Store base name too
                            lookup[unique_key] = info_copy
                            logging.debug(
                                f"Symbol lookup added (unique key): {unique_key} -> {info_copy}"
                            )
                    # Fallback or secondary index by full_name if needed, ensure no overwrite of unique_key entry
                    full_name_key = name  # Assuming 'name' is the full_name here
                    if (
                        full_name_key not in lookup and not unique_key
                    ):  # Only add if not already indexed by unique_key
                        info_copy = info.copy()
                        info_copy["type"] = symbol_type
                        info_copy["base_name"] = self._get_base_name(name)
                        lookup[full_name_key] = info_copy
                        logging.debug(
                            f"Symbol lookup added (full name fallback): {full_name_key} -> {info_copy}"
                        )
                    # --- 结束修改 ---
        logging.info(f"Symbol lookup table built with {len(lookup)} entries.")
        return lookup

    def find_main_functions(self):
        """
        查找可能的 main 函数入口点定义及其唯一键和位置。
        返回一个包含 (unique_key, location) 元组的列表。
        """
        main_funcs_with_loc = []
        # --- 修改：遍历 unique_keys ---
        for unique_key in self.defined_function_unique_keys:
            # 提取基本名称进行比较
            base_name = self._get_base_name_from_unique_key(unique_key)
            if base_name == "main":
                # 从 symbol_lookup 获取位置信息
                location = self.symbol_lookup.get(unique_key, {}).get(
                    "location", "[Location Unknown]"
                )
                main_funcs_with_loc.append((unique_key, location))
                logging.debug(
                    f"Found potential main function: {unique_key} at {location}"
                )
        # --- 结束修改 ---

        logging.info(
            f"Found {len(main_funcs_with_loc)} potential main function definitions."
        )
        return sorted(main_funcs_with_loc, key=lambda item: item[1])

    def _get_base_name(self, full_name_or_call_string):
        """辅助函数：从完整名称或调用字符串中提取基本名称。"""
        name = full_name_or_call_string
        if "::" in name:
            name = name.split("::")[-1]
        if "." in name:  # Handle obj.func()
            name = name.split(".")[-1]
        if "->" in name:  # Handle ptr->func()
            name = name.split("->")[-1]
        return name

    # --- 新增：从唯一键提取基本名称 ---
    def _get_base_name_from_unique_key(self, unique_key):
        """从 'path/file.cpp::Namespace::Class::func' 中提取 'func'"""
        if "::" in unique_key:
            return unique_key.split("::")[-1]
        return unique_key  # Should not happen for function defs, but as fallback

    # --- 结束新增 ---

    def resolve_call(self, calling_function_unique_key, called_func_str):
        """
        尝试将调用字符串解析为调用图中定义的函数的唯一键。
        Args:
            calling_function_unique_key (str): 进行调用的函数的唯一键。
            called_func_str (str): 在调用图中记录的被调用函数字符串。
        Returns:
            str or None: 匹配的被调用函数的唯一键，或 None。
        """
        base_called_name = self._get_base_name(called_func_str)
        caller_file = (
            calling_function_unique_key.split("::")[0]
            if "::" in calling_function_unique_key
            else None
        )

        # 1. 精确匹配 (如果 called_func_str 已经是 unique_key 格式) - 不太可能
        if called_func_str in self.defined_function_unique_keys:
            logging.debug(f"Resolved '{called_func_str}' directly to unique key.")
            return called_func_str

        # 2. 优先查找同一文件内匹配基本名称的函数
        possible_matches_in_file = []
        if caller_file:
            for unique_key in self.defined_function_unique_keys:
                # Ensure unique_key has the expected format before splitting
                if "::" in unique_key and unique_key.startswith(caller_file + "::"):
                    base_defined_name = self._get_base_name_from_unique_key(unique_key)
                    if base_called_name == base_defined_name:
                        possible_matches_in_file.append(unique_key)

        if len(possible_matches_in_file) == 1:
            resolved_key = possible_matches_in_file[0]
            logging.debug(
                f"Resolved '{called_func_str}' to '{resolved_key}' via base name in same file."
            )
            return resolved_key
        elif len(possible_matches_in_file) > 1:
            logging.warning(
                f"Ambiguous call '{called_func_str}' in {caller_file}. Multiple matches: {possible_matches_in_file}. Skipping resolution."
            )
            # Optionally, implement more sophisticated scope matching here
            return None  # Ambiguous

        # 3. 查找其他文件中匹配基本名称的函数 (可能不太准确，但作为后备)
        possible_matches_other_files = []
        for unique_key in self.defined_function_unique_keys:
            # Ensure unique_key has the expected format before checking startswith
            if "::" in unique_key and (
                not caller_file or not unique_key.startswith(caller_file + "::")
            ):
                base_defined_name = self._get_base_name_from_unique_key(unique_key)
                if base_called_name == base_defined_name:
                    possible_matches_other_files.append(unique_key)

        if len(possible_matches_other_files) == 1:
            resolved_key = possible_matches_other_files[0]
            logging.debug(
                f"Resolved '{called_func_str}' to '{resolved_key}' via base name in other file (fallback)."
            )
            return resolved_key
        elif len(possible_matches_other_files) > 1:
            logging.warning(
                f"Ambiguous call '{called_func_str}'. Multiple matches in other files: {possible_matches_other_files}. Skipping resolution."
            )
            return None  # Ambiguous

        logging.debug(
            f"Could not resolve call '{called_func_str}' from '{calling_function_unique_key}'."
        )
        return None

    def trace_call_chain(
        self, start_function_unique_key, visited=None, depth=0, max_depth=20
    ):
        """
        递归地追踪并打印从指定函数唯一键开始的调用链。
        Args:
            start_function_unique_key (str): 起始函数的唯一键。
            visited (set, optional): 用于检测循环的已访问函数唯一键集合。
            depth (int, optional): 当前递归深度。
            max_depth (int, optional): 最大递归深度。
        """
        indent = "    " * depth
        # --- 修改：使用 unique_key 查询位置 ---
        location_info = self.symbol_lookup.get(start_function_unique_key, {}).get(
            "location", "[Location Unknown]"
        )
        display_name = self._get_base_name_from_unique_key(
            start_function_unique_key
        )  # Display base name for clarity
        print(
            f"{indent}{colorama.Fore.WHITE}- {display_name} "  # Display base name
            f"{colorama.Fore.LIGHTBLACK_EX}({location_info}){colorama.Style.RESET_ALL}"
        )
        # --- 结束修改 ---

        if depth >= max_depth:
            print(
                f"{indent}  {colorama.Fore.YELLOW}[Max depth reached]{colorama.Style.RESET_ALL}"
            )
            return

        if visited is None:
            visited = set()

        # --- 修改：使用 unique_key 检测循环 ---
        if start_function_unique_key in visited:
            print(
                f"{indent}  {colorama.Fore.YELLOW}[Cycle detected]{colorama.Style.RESET_ALL}"
            )
            return
        visited.add(start_function_unique_key)
        # --- 结束修改 ---

        # --- 修改：使用 unique_key 获取调用 ---
        called_functions = self.call_graph.get(start_function_unique_key, set())
        # --- 结束修改 ---

        if not called_functions:
            pass

        sorted_called_functions = sorted(list(called_functions))

        for called_func_str in sorted_called_functions:
            # --- 修改：传递 unique_key 进行解析 ---
            resolved_callee_unique_key = self.resolve_call(
                start_function_unique_key, called_func_str
            )
            # --- 结束修改 ---

            if resolved_callee_unique_key:
                # --- 修改：递归传递 unique_key ---
                self.trace_call_chain(
                    resolved_callee_unique_key, visited.copy(), depth + 1, max_depth
                )
                # --- 结束修改 ---
            else:
                print(
                    f"{indent}  {colorama.Fore.RED}-> {called_func_str} [External/Unresolved/Member Call]{colorama.Style.RESET_ALL}"
                )

    def find_uncalled_functions(self):
        """
        查找所有已定义但从未在解析的调用图中被调用的函数（基于唯一键）。
        """
        # --- 修改：使用 unique_keys ---
        defined_functions_unique_keys = self.defined_function_unique_keys
        resolved_called_unique_keys = set()

        logging.debug("Starting search for uncalled functions...")
        logging.debug(
            f"Total defined function unique keys: {len(defined_functions_unique_keys)}"
        )

        for defining_func_unique_key, called_set in self.call_graph.items():
            for called_func_str in called_set:
                resolved_callee_unique_key = self.resolve_call(
                    defining_func_unique_key, called_func_str
                )
                if resolved_callee_unique_key:
                    if resolved_callee_unique_key not in resolved_called_unique_keys:
                        logging.debug(
                            f"Adding '{resolved_callee_unique_key}' to resolved called unique keys set."
                        )
                        resolved_called_unique_keys.add(resolved_callee_unique_key)

        logging.debug(
            f"Total resolved called unique keys: {len(resolved_called_unique_keys)}"
        )

        uncalled_unique_keys = (
            defined_functions_unique_keys - resolved_called_unique_keys
        )
        logging.info(f"Found {len(uncalled_unique_keys)} uncalled functions.")

        return sorted(list(uncalled_unique_keys))
        # --- 结束修改 ---

    def interactive_trace(self):
        """提供交互式菜单，让用户选择 main 函数并开始追踪（使用唯一键）。"""
        print(
            f"\n{colorama.Fore.CYAN}--- Call Chain Tracer ---{colorama.Style.RESET_ALL}"
        )
        # --- 修改：获取包含 unique_key 的 main 函数列表 ---
        main_definitions = (
            self.find_main_functions()
        )  # Returns list of (unique_key, location)
        selected_main_unique_key = None
        # --- 结束修改 ---

        if not main_definitions:
            print("No 'main' function definition found to start tracing.")
        elif len(main_definitions) == 1:
            # --- 修改：使用 unique_key ---
            selected_main_unique_key, location = main_definitions[0]
            display_name = self._get_base_name_from_unique_key(selected_main_unique_key)
            print(
                f"{colorama.Fore.GREEN}Found one main function definition: {colorama.Fore.WHITE}{display_name} "
                f"{colorama.Fore.LIGHTBLACK_EX}({location}){colorama.Style.RESET_ALL}"
            )
            # --- 结束修改 ---
        else:
            print("Multiple 'main' function definitions found:")
            # --- 修改：显示 unique_key 和 location ---
            for i, (unique_key, location) in enumerate(main_definitions):
                display_name = self._get_base_name_from_unique_key(unique_key)
                print(
                    f"  {i + 1}: {colorama.Fore.WHITE}{display_name} {colorama.Fore.LIGHTBLACK_EX}({location}){colorama.Style.RESET_ALL}"
                )
            # --- 结束修改 ---

            # 使用 while True 循环，直到获得有效选择或用户选择跳过
            while True:
                try:
                    # 提示输入用黄色
                    choice = input(
                        f"{colorama.Fore.YELLOW}Select a main function definition to trace (1-{len(main_definitions)}), or 0 to skip tracing: {colorama.Style.RESET_ALL}"
                    )
                    index = int(choice) - 1
                    if index == -1:
                        selected_main_unique_key = None
                        break
                    if 0 <= index < len(main_definitions):
                        # --- 修改：获取选择的 unique_key ---
                        selected_main_unique_key, _ = main_definitions[index]
                        # --- 结束修改 ---
                        break
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
        # --- 修改：使用 unique_key ---
        if selected_main_unique_key:
            display_name = self._get_base_name_from_unique_key(selected_main_unique_key)
            location = self.symbol_lookup.get(selected_main_unique_key, {}).get(
                "location", "?"
            )
            print(
                f"\nTracing call chain starting from: {colorama.Fore.WHITE}{display_name} ({location}){colorama.Style.RESET_ALL}\n"
            )
            self.trace_call_chain(selected_main_unique_key)
            print(
                f"\n{colorama.Fore.CYAN}--- End Call Chain Trace ---{colorama.Style.RESET_ALL}"
            )
        # --- 结束修改 ---
        else:
            # 只有在找到了 main 函数定义但用户选择跳过时才打印此消息
            if main_definitions:
                print("\nSkipping call chain tracing.")

        # --- 列出未调用函数 ---
        print(
            f"\n{colorama.Fore.CYAN}--- Uncalled Functions ---{colorama.Style.RESET_ALL}"
        )
        # --- 修改：处理 unique_keys ---
        uncalled_unique_keys = self.find_uncalled_functions()
        if uncalled_unique_keys:
            print("The following defined functions appear to be uncalled:")
            for unique_key in uncalled_unique_keys:
                location_info = self.symbol_lookup.get(unique_key, {}).get(
                    "location", "[Location Unknown]"
                )
                display_name = self._get_base_name_from_unique_key(unique_key)
                print(
                    f"- {colorama.Fore.YELLOW}{display_name} {colorama.Fore.LIGHTBLACK_EX}({location_info}){colorama.Style.RESET_ALL}"
                )
        # --- 结束修改 ---
        else:
            # 成功信息用绿色
            print(
                f"{colorama.Fore.GREEN}All defined functions appear to be called within the analyzed scope.{colorama.Style.RESET_ALL}"
            )
        print(
            f"{colorama.Fore.CYAN}--- End Uncalled Functions ---{colorama.Style.RESET_ALL}"
        )

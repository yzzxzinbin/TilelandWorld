import logging
import os
import re


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
        # 当前定义的函数名 (用于关联调用)
        self.current_defining_function_full_name = None

        # 添加括号跟踪计数和多行括号模式标志
        self.unclosed_paren_count = 0  # 未闭合的圆括号数量
        self.in_multiline_paren = False  # 是否处于多行括号模式
        self.multiline_paren_content = []  # 收集多行括号内容
        self.line_start_pos = 0  # 当前行在处理时的起始位置

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
        # 如果进入函数定义状态，记录函数名
        elif new_state == self.FUNCTION_DEF and scope_name:
            self.current_defining_function_full_name = scope_name
            logging.debug(f"Entered function definition: {scope_name}")

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
        # 如果退出函数定义状态，清除记录的函数名
        elif old_state == self.FUNCTION_DEF:
            if self.current_defining_function_full_name:
                logging.debug(
                    f"Exited function definition: {self.current_defining_function_full_name}"
                )
                self.current_defining_function_full_name = None

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
            # 更新未闭合圆括号计数
            self.unclosed_paren_count += 1
        elif char == ")":
            if self.paren_stack:
                self.paren_stack.pop()
                # 更新未闭合圆括号计数
                if self.unclosed_paren_count > 0:
                    self.unclosed_paren_count -= 1

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
        self.line_start_pos = 0  # 重置行起始位置
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

    def start_line_tracking(self):
        """标记处理新行的开始，重置行位置计数"""
        self.line_start_pos = 0

    def advance_position(self):
        """前进一个字符位置"""
        self.line_start_pos += 1

    def is_start_of_line(self):
        """检查当前是否在行的开始位置（考虑空白字符）"""
        return self.line_start_pos == 0

    def has_balanced_parens(self):
        """检查括号是否平衡"""
        return self.unclosed_paren_count == 0

    def enter_multiline_paren_mode(self, line):
        """进入多行括号模式"""
        self.in_multiline_paren = True
        self.multiline_paren_content = [line]
        logging.debug(f"Entering multiline paren mode with line: '{line}'")

    def add_to_multiline_paren(self, line):
        """添加行到多行括号内容"""
        self.multiline_paren_content.append(line)
        logging.debug(f"Adding to multiline paren: '{line}'")

    def get_multiline_paren_content(self):
        """获取合并的多行括号内容"""
        combined = " ".join(self.multiline_paren_content)
        logging.debug(f"Combined multiline paren content: '{combined}'")
        return combined

    def exit_multiline_paren_mode(self):
        """退出多行括号模式"""
        self.in_multiline_paren = False
        content = self.multiline_paren_content.copy()
        self.multiline_paren_content = []
        logging.debug("Exiting multiline paren mode")
        return content

    def reset(self):
        """重置状态机"""
        self.__init__()
        # 确保新属性也被重置
        self.current_defining_function_full_name = None
        self.unclosed_paren_count = 0
        self.in_multiline_paren = False
        self.multiline_paren_content = []
        self.line_start_pos = 0

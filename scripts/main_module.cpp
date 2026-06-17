// 极简版 main_module.cpp
import huge_templates;

int main() {
    // 直接调用模块函数，并将其 double 结果强转为 int 返回
    // 整个文件除了模块导入，没有任何文本预处理解析
    double result = call_all_templates(1.0);
    return static_cast<int>(result);
}
#include <iostream>
#include <string>
#include <giac.h>

using namespace std;
using namespace giac;

// 封装一个简单的执行器：输入指令，输出结果字符串
string execute(const string& command, context* ctx) {
    try {
        // 第一步：将字符串解析为 gen 对象（内部会处理 ASCIIMATH）
        gen g(command, ctx);

        // 第二步：执行运算
        // 第二个参数 1 表示执行级别
        gen result = eval(g, 1, ctx);

        // 返回结果的打印形式
        return result.print(ctx);
    } catch (std::runtime_error& e) {
        return string("Error: ") + e.what();
    }
}

int main() {
    context ctx;

    cout << "=== Giac 字符串指令测试 (ASCIIMATH 风格) ===" << endl;

    // 1. 符号积分
    // 直接写完整的指令字符串
    string i1 = "int(x^2 * sin(x)*sin(x)*sin(x), x)";
    cout << "积分 [In]: " << i1 << endl;
    cout << "     [Out]: " << execute(i1, &ctx) << endl << endl;

    // 2. 符号极限
    string l1 = "limit(sin(x)/x, x, 0)";
    cout << "极限 [In]: " << l1 << endl;
    cout << "     [Out]: " << execute(l1, &ctx) << endl << endl;

    // 3. 数值积分 (使用 evalf 强制转浮点)
    string n1 = "evalf(integrate(exp(-x^2), x, 0, 1))";
    cout << "数值积分 [In]: " << n1 << endl;
    cout << "         [Out]: " << execute(n1, &ctx) << endl << endl;

    // 4. 方程求解 (演示 CAS 的强大)
    string s1 = "solve(x^2 - 3x + 2 = 0, x)";
    cout << "解方程 [In]: " << s1 << endl;
    cout << "       [Out]: " << execute(s1, &ctx) << endl << endl;

    // 5. 矩阵运算
    string m1 = "[[1,2],[3,4]]^2";
    cout << "矩阵平方 [In]: " << m1 << endl;
    cout << "         [Out]: " << execute(m1, &ctx) << endl << endl;

    return 0;
}
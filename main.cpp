#include <iostream>
#include <string>
#include <exception>
#include <cstdio>
#include <locale>
#include <vector>
#include <memory>
#include <unordered_map>

// 1. 引入 FreeType 与 HarfBuzz
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

// 2. 引入 MicroTeX 头文件
#include <latex.h>

using namespace std;
using namespace tex;

namespace StuCanvas {

// ============================================================================
// 字体与缓存全局持有者
// ============================================================================
static FT_Library g_ft_lib;
static std::unordered_map<std::string, FT_Face> g_faces_cache;

static FT_Face GetFace(const std::string& filepath) {
    auto it = g_faces_cache.find(filepath);
    if (it != g_faces_cache.end()) {
        return it->second;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(g_ft_lib, filepath.c_str(), 0, &face) == 0) {
        g_faces_cache[filepath] = face;
        return face;
    }
    return nullptr;
}

/**
 * @brief 高安全性降级链（已消除 Condition is always true 警告）
 */
    /**
     * @brief 高安全性降级链（已适配 Arch Linux 拆分版 Noto Sans CJK OTF 命名规范）
     */
    static FT_Face GetDefaultFace() {
    // 优先尝试加载系统自带的 XITSMath 作为公式兜底
    FT_Face face = GetFace("/usr/local/share/microtex/res/XITSMath-Regular.otf");

    // 1. 【核心修正】：优先探测 Arch Linux 官方 noto-fonts-cjk 拆分出来的简体中文字体 (SC)
    // 简体中文字体中通常包含了全套中日韩文字形，足以完美绘制你的 "中文" 与 "한국어"
    if (!face) face = GetFace("/usr/share/fonts/noto-cjk/NotoSansCJKsc-Regular.otf");
    if (!face) face = GetFace("/usr/share/fonts/noto-cjk/NotoSansCJKsc-Regular.ttc");

    // 2. 探测 Arch Linux 官方韩文字体 (KR)
    if (!face) face = GetFace("/usr/share/fonts/noto-cjk/NotoSansCJKkr-Regular.otf");

    // 3. 其它系统复合字集路径
    if (!face) face = GetFace("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc");
    if (!face) face = GetFace("/usr/share/fonts/noto/NotoSansCJK-Regular.ttc");
    if (!face) face = GetFace("/usr/share/fonts/TTF/wqy-microhei.ttc");
    if (!face) face = GetFace("/usr/share/fonts/TTF/dejavu/DejaVuSans.ttf");
    if (!face) face = GetFace("/usr/local/share/microtex/res/fonts/latin/cmr10.ttf");

    return face;
}
// ============================================================================
// A. 桥接字体：StuFont
// ============================================================================
class StuFont : public tex::Font {
public:
    FT_Face ft_face;
    float font_size;

    StuFont(FT_Face face, float size) : ft_face(face), font_size(size) {}
    virtual ~StuFont() override = default;

    float getSize() const override { return font_size; }

    sptr<tex::Font> deriveFont(int style) const override {
        return std::make_shared<StuFont>(ft_face, font_size);
    }

    bool operator==(const tex::Font& f) const override {
        auto* other = dynamic_cast<const StuFont*>(&f);
        return other && ft_face == other->ft_face && font_size == other->font_size;
    }

    bool operator!=(const tex::Font& f) const override {
        return !(*this == f);
    }
};

// ============================================================================
// B. HarfBuzz 连字排版：StuTextLayout
// ============================================================================
struct ShapedGlyph {
    uint32_t glyph_id;
    float offset_x, offset_y, advance_x;
};

    class StuTextLayout : public tex::TextLayout {
    public:
        std::vector<ShapedGlyph> shaped_glyphs;
        float layout_width = 0.0f;
        float layout_height = 0.0f;

        StuTextLayout(const std::wstring& src, const std::shared_ptr<StuFont>& stu_font) {
            if (!stu_font || !stu_font->ft_face) {
                std::cerr << "[Error] StuTextLayout: ft_face is null! Skipping HarfBuzz shaping." << std::endl;
                return;
            }

            // ====================================================================
            // 【核心修复】：在交给 HarfBuzz 测量前，必须将共享 FT_Face 的尺寸调整到当前需要的 font_size
            // 这一步能让 FreeType 产出正确的像素 metrics，从而让 HarfBuzz 算出正确的字间距（x_advance）！
            // ====================================================================
            FT_Set_Pixel_Sizes(stu_font->ft_face, 0, static_cast<FT_UInt>(stu_font->font_size));

            hb_buffer_t* hb_buf = hb_buffer_create();
            hb_buffer_add_utf16(hb_buf, reinterpret_cast<const uint16_t*>(src.data()), src.length(), 0, src.length());
            hb_buffer_guess_segment_properties(hb_buf);

            hb_font_t* hb_font = hb_ft_font_create(stu_font->ft_face, nullptr);
            hb_shape(hb_font, hb_buf, nullptr, 0);

            unsigned int glyph_count;
            hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(hb_buf, &glyph_count);
            hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(hb_buf, &glyph_count);

            float current_x = 0.0f;
            for (unsigned int i = 0; i < glyph_count; i++) {
                ShapedGlyph sg;
                sg.glyph_id = glyph_info[i].codepoint;
                // 累加字距：当前光标位置 + 字符相对微调量
                sg.offset_x = current_x + (glyph_pos[i].x_offset / 64.0f);
                sg.offset_y = glyph_pos[i].y_offset / 64.0f;
                sg.advance_x = glyph_pos[i].x_advance / 64.0f;
                shaped_glyphs.push_back(sg);

                // 步进光标位置，为下一个字符奠定正确的起点 X 坐标
                current_x += sg.advance_x;
            }
            layout_width = current_x;
            layout_height = stu_font->font_size;

            hb_buffer_destroy(hb_buf);
            hb_font_destroy(hb_font);
        }



        void getBounds(tex::Rect& rect) override {
            rect.x = 0; rect.y = 0;
            rect.w = layout_width; rect.h = layout_height;
        }

        // 【核心修复】：在这里将排版信号分发给我们的自定义画布进行物理拦截和数据打印
        void draw(tex::Graphics2D& g2, float x, float y) override;
    };


// ============================================================================
// C. 纯净拦截画布：StuGraphics2D
// ============================================================================
class StuGraphics2D : public tex::Graphics2D {
private:
    float _tx = 0.0f, _ty = 0.0f, _sx = 1.0f, _sy = 1.0f;
    tex::color _color = 0xFF000000;
    tex::Stroke _stroke;
    const StuFont* _current_font = nullptr;

public:
    StuGraphics2D() = default;


    void setColor(tex::color c) override { _color = c; }
    tex::color getColor() const override { return _color; }
    void setStroke(const tex::Stroke& s) override { _stroke = s; }
    const tex::Stroke& getStroke() const override { return _stroke; }
    void setStrokeWidth(float w) override { _stroke.lineWidth = w; }
    const tex::Font* getFont() const override { return _current_font; }
    void setFont(const tex::Font* font) override {
        _current_font = static_cast<const StuFont*>(font);
    }

    void translate(float dx, float dy) override { _tx += dx * _sx; _ty += dy * _sy; }
    void scale(float sx, float sy) override { _sx *= sx; _sy *= sy; }
    void reset() override { _tx = 0; _ty = 0; _sx = 1; _sy = 1; }
    float sx() const override { return _sx; }
    float sy() const override { return _sy; }

    void rotate(float angle) override {}
    void rotate(float angle, float px, float py) override {}

    void drawChar(wchar_t c, float x, float y) override {
        float global_x = _tx + x * _sx;
        float global_y = _ty + y * _sy;
        std::printf("  * [Glyph] U+%04X ('%lc') | Global: (%.2f, %.2f)\n", static_cast<unsigned int>(c), (c < 128 && c >= 32) ? c : ' ', global_x, global_y);
    }

    void drawText(const std::wstring& t, float x, float y) override {
        float global_x = _tx + x * _sx;
        float global_y = _ty + y * _sy;
        std::wcout << L"  * [Text] \"" << t << L"\" at (" << _tx + x * _sx << L")" << std::endl;
    }

    // 【新增核心接口】：由 StuTextLayout 回调，直接打印出 HarfBuzz 连字整形后的每一个字符 ID 和物理坐标
    // 这也是你未来在 Vulkan 中调用 FreeType 的起点！
    void drawShapedGlyphs(const std::vector<ShapedGlyph>& glyphs, float x, float y) {
        for (const auto& sg : glyphs) {
            float gx = _tx + (x + sg.offset_x) * _sx;
            float gy = _ty + (y + sg.offset_y) * _sy;
            std::printf("  * [Shaped Glyph (HB)] Font GlyphID: %d | Global: (%.2f, %.2f)\n", sg.glyph_id, gx, gy);
        }
    }

    void drawLine(float x1, float y1, float x2, float y2) override {
        std::printf("  * [Line] (%.2f, %.2f)->(%.2f, %.2f)\n", _tx + x1 * _sx, _ty + y1 * _sy, _tx + x2 * _sx, _ty + y2 * _sy);
    }
    void drawRect(float x, float y, float w, float h) override {}
    void fillRect(float x, float y, float w, float h) override {
        std::printf("  * [FillRect] Global: (%.2f, %.2f)\n", _tx + x * _sx, _ty + y * _sy);
    }

    void drawRoundRect(float x, float y, float w, float h, float rx, float ry) override {}
    void fillRoundRect(float x, float y, float w, float h, float rx, float ry) override {}
};

// ============================================================================
// 在此处实现 StuTextLayout::draw 避免循环引用
// ============================================================================
inline void StuTextLayout::draw(tex::Graphics2D& g2, float x, float y) {
    auto& stu_g2 = static_cast<StuGraphics2D&>(g2);
    stu_g2.drawShapedGlyphs(shaped_glyphs, x, y);
}

} // namespace StuCanvas

// ============================================================================
// 【核心安全重写】：静态工厂注入实现
// ============================================================================
namespace tex {

Font* Font::create(const std::string& file, float size) {
    FT_Face face = StuCanvas::GetFace(file);
    if (!face) {
        face = StuCanvas::GetDefaultFace();
    }
    return new StuCanvas::StuFont(face, size);
}

sptr<Font> Font::_create(const std::string& name, int style, float size) {
    return std::make_shared<StuCanvas::StuFont>(StuCanvas::GetDefaultFace(), size);
}

sptr<TextLayout> TextLayout::create(const std::wstring& src, const sptr<Font>& font) {
    // 使用 modern C++ static_pointer_cast
    auto stu_font = std::static_pointer_cast<StuCanvas::StuFont>(font);
    return std::make_shared<StuCanvas::StuTextLayout>(src, stu_font);
}

} // namespace tex

int main() {
    std::locale::global(std::locale(""));
    std::wcout.imbue(std::locale(""));

    std::cout << "==========================================================" << std::endl;
    std::cout << "  StuCanvas LaTeX Integration (Secure & Clean Core)      " << std::endl;
    std::cout << "==========================================================" << std::endl;

    try {
        // 1. 初始化 FreeType 句柄
        if (FT_Init_FreeType(&StuCanvas::g_ft_lib) != 0) {
            std::cerr << "FreeType: Failed to init library." << std::endl;
            return -1;
        }

        // 2. 初始化 LaTeX 核心
        tex::LaTeX::init("/usr/local/share/microtex/res");
        std::cout << "[System] LaTeX math engine initialized cleanly." << std::endl;

        // 3. 准备公式
        std::wstring latex_code = tex::utf82wide("\\frac{\\text{中文}}{\\sqrt{\\text{한국어}}}");

        // 4. 【核心重构】：使用现代 C++17 带初始化的 if 表达式，彻底消除变量移动建议
        if (auto render = tex::LaTeX::parse(latex_code, 800, 24, 10, 0xFF000000)) {
            std::cout << "[Layout] Render bounds: " << render->getWidth() << "x" << render->getHeight() << std::endl;
            std::cout << "\n--- START OF DATA SIGNALS ---\n" << std::endl;

            StuCanvas::StuGraphics2D dumper;
            render->draw(dumper, 20.0f, 20.0f);

            std::cout << "\n--- END OF DATA SIGNALS ---\n" << std::endl;
            delete render;
        }

        // 5. 清理资源并退出
        tex::LaTeX::release();

        // 【核心重构】：利用 C++17 结构化绑定（Structured Bindings）遍历 Map，消除 views::values 建议
        for (auto& [path, face] : StuCanvas::g_faces_cache) {
            if (face) {
                FT_Done_Face(face);
            }
        }
        FT_Done_FreeType(StuCanvas::g_ft_lib);
        std::cout << "[System] Resources cleaned up. Safe exit." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}
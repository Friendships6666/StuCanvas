#ifndef STUSCRIPT_LEXER_HPP
#define STUSCRIPT_LEXER_HPP

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include <cstdint>

namespace StuScript {

enum class TokenType : uint8_t {
    LET, FN, RETURN, IF, ELSE, WHILE, STRUCT, ALIAS,
    IDENTIFIER, NUMBER, STRING,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    COLON, SEMICOLON, COMMA, DOT, ARROW,
    EQUAL, PLUS, MINUS, STAR, SLASH, PERCENT, AMPERSAND, DOT_STAR,
    EQ_EQ, NOT_EQ, LT, GT, LT_EQ, GT_EQ,
    EOF_TOKEN, INVALID, FOR, AND_AND, OR_OR, BREAK, CONTINUE, ELIF,
    AS
};

struct alignas(8) Token {
    llvm::StringRef lexeme;
    uint32_t line;
    uint32_t column;
    TokenType type;
    bool is(TokenType t) const { return type == t; }
};

struct CharTraits {
    enum : uint8_t { IsDigit = 1 << 0, IsAlpha = 1 << 1, IsSpace = 1 << 2, IsIdent = 1 << 3 };
    uint8_t table[256] = {0};
    constexpr CharTraits() {
        for (int i = '0'; i <= '9'; ++i) table[i] |= IsDigit | IsIdent;
        for (int i = 'a'; i <= 'z'; ++i) table[i] |= IsAlpha | IsIdent;
        for (int i = 'A'; i <= 'Z'; ++i) table[i] |= IsAlpha | IsIdent;
        table[(uint8_t)'_'] |= IsAlpha | IsIdent;
        // 支持 UTF-8 (128-255 字节全部视为标识符组成部分)
        for (int i = 128; i < 256; ++i) table[i] |= IsAlpha | IsIdent;
        table[(uint8_t)' '] |= IsSpace;
        table[(uint8_t)'\t'] |= IsSpace;
        table[(uint8_t)'\r'] |= IsSpace;
        table[(uint8_t)'\n'] |= IsSpace;
    }
};

static constexpr CharTraits GTraits;

class Lexer {
public:
    explicit Lexer(llvm::StringRef source)
        : cur_ptr_(source.data()), end_ptr_(source.data() + source.size()),
          line_(1), line_start_(source.data()) {}

    void scanAll(llvm::SmallVectorImpl<Token>& tokens) {
        tokens.reserve(tokens.size() + (end_ptr_ - cur_ptr_) / 5);
        while (cur_ptr_ < end_ptr_) {
            skipWhitespace();
            if (cur_ptr_ >= end_ptr_) break;
            tokens.push_back(nextToken());
        }
        tokens.push_back({llvm::StringRef(end_ptr_, 0), line_, getColumn(), TokenType::EOF_TOKEN});
    }

private:
    const char* cur_ptr_;
    const char* const end_ptr_;
    uint32_t line_;
    const char* line_start_;

    inline uint32_t getColumn() const { return static_cast<uint32_t>(cur_ptr_ - line_start_) + 1; }

    void skipWhitespace() {
        while (cur_ptr_ < end_ptr_) {
            uint8_t c = (uint8_t)*cur_ptr_;
            if (GTraits.table[c] & CharTraits::IsSpace) {
                if (c == '\n') { line_++; line_start_ = cur_ptr_ + 1; }
                cur_ptr_++;
            } else if (c == '/' && (cur_ptr_ + 1 < end_ptr_) && cur_ptr_[1] == '/') {
                cur_ptr_ += 2;
                while (cur_ptr_ < end_ptr_ && *cur_ptr_ != '\n') cur_ptr_++;
            } else break;
        }
    }

    Token nextToken() {
        const char* token_start = cur_ptr_;
        uint8_t c = (uint8_t)*cur_ptr_++;
        uint8_t traits = GTraits.table[c];

        if (traits & CharTraits::IsAlpha) return identifier(token_start);
        if (traits & CharTraits::IsDigit) return number(token_start);

        uint32_t col = static_cast<uint32_t>(token_start - line_start_) + 1;

        switch (c) {
            case '(': return { {token_start, 1}, line_, col, TokenType::LPAREN };
            case ')': return { {token_start, 1}, line_, col, TokenType::RPAREN };
            case '{': return { {token_start, 1}, line_, col, TokenType::LBRACE };
            case '}': return { {token_start, 1}, line_, col, TokenType::RBRACE };
            case '[': return { {token_start, 1}, line_, col, TokenType::LBRACKET };
            case ']': return { {token_start, 1}, line_, col, TokenType::RBRACKET };
            case ',': return { {token_start, 1}, line_, col, TokenType::COMMA };
            case ';': return { {token_start, 1}, line_, col, TokenType::SEMICOLON };
            case ':': return { {token_start, 1}, line_, col, TokenType::COLON };
            case '+': return { {token_start, 1}, line_, col, TokenType::PLUS };
            case '*': return { {token_start, 1}, line_, col, TokenType::STAR };
            case '%': return { {token_start, 1}, line_, col, TokenType::PERCENT };
            case '-':
                if (cur_ptr_ < end_ptr_ && *cur_ptr_ == '>') { cur_ptr_++; return { {token_start, 2}, line_, col, TokenType::ARROW }; }
                return { {token_start, 1}, line_, col, TokenType::MINUS };
            case '.':
                if (cur_ptr_ < end_ptr_ && *cur_ptr_ == '*') { cur_ptr_++; return { {token_start, 2}, line_, col, TokenType::DOT_STAR }; }
                return { {token_start, 1}, line_, col, TokenType::DOT };
            case '=':
                if (cur_ptr_ < end_ptr_ && *cur_ptr_ == '=') { cur_ptr_++; return { {token_start, 2}, line_, col, TokenType::EQ_EQ }; }
                return { {token_start, 1}, line_, col, TokenType::EQUAL };
            case '!':
                if (cur_ptr_ < end_ptr_ && *cur_ptr_ == '=') { cur_ptr_++; return { {token_start, 2}, line_, col, TokenType::NOT_EQ }; }
                break;
            case '<':
                if (cur_ptr_ < end_ptr_ && *cur_ptr_ == '=') { cur_ptr_++; return { {token_start, 2}, line_, col, TokenType::LT_EQ }; }
                return { {token_start, 1}, line_, col, TokenType::LT };
            case '>':
                if (cur_ptr_ < end_ptr_ && *cur_ptr_ == '=') { cur_ptr_++; return { {token_start, 2}, line_, col, TokenType::GT_EQ }; }
                return { {token_start, 1}, line_, col, TokenType::GT };
            case '&':
                if (cur_ptr_ < end_ptr_ && *cur_ptr_ == '&') {
                    cur_ptr_++;
                    return { {token_start, 2}, line_, col, TokenType::AND_AND };
                }
                return { {token_start, 1}, line_, col, TokenType::AMPERSAND };
            case '|':
                if (cur_ptr_ < end_ptr_ && *cur_ptr_ == '|') {
                    cur_ptr_++;
                    return { {token_start, 2}, line_, col, TokenType::OR_OR };
                }
                break;
            case '"': return string(token_start);
        }
        return { {token_start, 1}, line_, col, TokenType::INVALID };
    }

    Token identifier(const char* start)
    {
        while (cur_ptr_ < end_ptr_ && (GTraits.table[(uint8_t)*cur_ptr_] & CharTraits::IsIdent)) cur_ptr_++;
        llvm::StringRef text(start, cur_ptr_ - start);
        TokenType type = llvm::StringSwitch<TokenType>(text)
            .Case("as", TokenType::AS)
            .Case("break", TokenType::BREAK)
            .Case("continue", TokenType::CONTINUE)
            .Case("elif", TokenType::ELIF)
            .Case("let", TokenType::LET)
            .Case("fn", TokenType::FN)
            .Case("return", TokenType::RETURN)
            .Case("if", TokenType::IF)
            .Case("else", TokenType::ELSE)
            .Case("while", TokenType::WHILE)
            .Case("struct", TokenType::STRUCT)
            .Case("for", TokenType::FOR)
            .Case("alias", TokenType::ALIAS)
            .Default(TokenType::IDENTIFIER);
        return {text, line_, static_cast<uint32_t>(start - line_start_) + 1, type};
    }

    Token number(const char* start) {
        // 1. 读取整数部分
        while (cur_ptr_ < end_ptr_ && (GTraits.table[(uint8_t)*cur_ptr_] & CharTraits::IsDigit)) cur_ptr_++;

        // 2. 读取小数部分
        if (cur_ptr_ < end_ptr_ && *cur_ptr_ == '.' && (cur_ptr_ + 1 < end_ptr_) && (GTraits.table[(uint8_t)cur_ptr_[1]] & CharTraits::IsDigit)) {
            cur_ptr_++;
            while (cur_ptr_ < end_ptr_ && (GTraits.table[(uint8_t)*cur_ptr_] & CharTraits::IsDigit)) cur_ptr_++;
        }

        // 3. 读取类型后缀 (例如 f32, u64, i8)
        while (cur_ptr_ < end_ptr_ &&
               ((GTraits.table[(uint8_t)*cur_ptr_] & CharTraits::IsAlpha) ||
                (GTraits.table[(uint8_t)*cur_ptr_] & CharTraits::IsDigit))) {
            cur_ptr_++;
        }

        return { {start, static_cast<size_t>(cur_ptr_ - start)}, line_, static_cast<uint32_t>(start - line_start_) + 1, TokenType::NUMBER };
    }

    Token string(const char* start) {
        while (cur_ptr_ < end_ptr_ && *cur_ptr_ != '"') {
            if (*cur_ptr_ == '\n') { line_++; line_start_ = cur_ptr_ + 1; }
            cur_ptr_++;
        }
        if (cur_ptr_ >= end_ptr_) return { {start, static_cast<size_t>(cur_ptr_ - start)}, line_, getColumn(), TokenType::INVALID };
        cur_ptr_++; return { {start, static_cast<size_t>(cur_ptr_ - start)}, line_, static_cast<uint32_t>(start - line_start_) + 1, TokenType::STRING };
    }
};

} // namespace StuScript

#endif
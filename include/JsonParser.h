#pragma once
#include "Scene.h"
#include <string>

// Minimal JSON parser for the path tracer config
// Only supports the fixed structure we need:
//   { "render": {...}, "camera": {...}, "objects": [...], "lighting": {...}, "sky": {...} }
class JsonParser {
public:
    static SceneConfig parse(const std::string& filename);

private:
    struct Token {
        enum Type { STRING, NUMBER, LBRACE, RBRACE, LBRACKET, RBRACKET, COLON, COMMA, TRUE_T, FALSE_T, NULL_T, END };
        Type type;
        std::string value;
    };

    class Lexer {
    public:
        Lexer(const std::string& input) : input(input), pos(0) {}
        Token next();
    private:
        void skipWhitespace();
        Token readString();
        Token readNumber();
        Token readKeyword();
        std::string input;
        size_t pos;
    };

    static void parseRender(Lexer& lex, SceneConfig& cfg);
    static void parseCamera(Lexer& lex, SceneConfig& cfg);
    static void parseObjects(Lexer& lex, SceneConfig& cfg);
    static void parseLighting(Lexer& lex, SceneConfig& cfg);
    static void parseSky(Lexer& lex, SceneConfig& cfg);
    static void parseObject(Lexer& lex, SceneConfig::ObjectEntry& obj);

    static float expectNumber(Lexer& lex);
    static std::string expectString(Lexer& lex);
    static void expectToken(Lexer& lex, Token::Type type);
    static Vec3f parseVec3(Lexer& lex);
    static void skipValue(Lexer& lex);
};

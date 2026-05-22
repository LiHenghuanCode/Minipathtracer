#pragma once
#include "Scene.h"
#include <string>

// Minimal JSON parser for the path tracer config.
// Supports the native renderer config plus Blender-exported "cameras" and
// "lights" arrays, either inline or through a "sceneData" file reference.
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
    static void parseCameras(Lexer& lex, SceneConfig& cfg);
    static void parseLights(Lexer& lex, SceneConfig& cfg);
    static void parseObject(Lexer& lex, SceneConfig::ObjectEntry& obj);

    static float expectNumber(Lexer& lex);
    static bool expectBool(Lexer& lex);
    static std::string expectString(Lexer& lex);
    static void expectToken(Lexer& lex, Token::Type type);
    static Vec3f parseVec3(Lexer& lex);
    static void parseMatrix4(Lexer& lex, float matrix[4][4]);
    static Vec3f blenderToRendererPoint(const Vec3f& v);
    static Vec3f blenderToRendererVector(const Vec3f& v);
    static void parseInto(const std::string& filename, SceneConfig& cfg);
    static void skipValue(Lexer& lex);
};

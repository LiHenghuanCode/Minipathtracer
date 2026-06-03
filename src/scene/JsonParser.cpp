#include "scene/JsonParser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <filesystem>

namespace {
std::filesystem::path resolveInputPath(const std::string& filename) {
    std::filesystem::path path(filename);
    if (path.is_absolute() && std::filesystem::exists(path)) {
        return path;
    }

    if (std::filesystem::exists(path)) {
        return path;
    }

    if (path.is_relative()) {
        std::filesystem::path probe = std::filesystem::current_path();
        while (true) {
            std::filesystem::path candidate = probe / path;
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
            if (probe == probe.root_path()) {
                break;
            }
            probe = probe.parent_path();
        }
    }

    return path;
}

std::string resolveAssetPath(const std::filesystem::path& baseDir, const std::string& value) {
    std::filesystem::path path(value);
    if (!path.is_relative()) {
        return path.string();
    }

    std::filesystem::path configRelative = baseDir / path;
    if (std::filesystem::exists(configRelative)) {
        return configRelative.string();
    }

    return resolveInputPath(value).string();
}
}

void JsonParser::Lexer::skipWhitespace() {
    while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t' ||
           input[pos] == '\n' || input[pos] == '\r')) {
        pos++;
    }
}

JsonParser::Token JsonParser::Lexer::readString() {
    pos++;
    std::string result;
    while (pos < input.size() && input[pos] != '"') {
        if (input[pos] == '\\') {
            pos++;
            if (pos < input.size()) {
                switch (input[pos]) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    default: result += input[pos]; break;
                }
            }
        } else {
            result += input[pos];
        }
        pos++;
    }
    pos++;
    return {Token::STRING, result};
}

JsonParser::Token JsonParser::Lexer::readNumber() {
    size_t start = pos;
    if (input[pos] == '-') pos++;
    while (pos < input.size() && (isdigit(input[pos]) || input[pos] == '.' || input[pos] == 'e' || input[pos] == 'E' || input[pos] == '+' || input[pos] == '-')) {
        if ((input[pos] == 'e' || input[pos] == 'E' || input[pos] == '+' || input[pos] == '-') && pos == start) break;
        pos++;
    }
    return {Token::NUMBER, input.substr(start, pos - start)};
}

JsonParser::Token JsonParser::Lexer::readKeyword() {
    size_t start = pos;
    while (pos < input.size() && isalpha(input[pos])) pos++;
    std::string word = input.substr(start, pos - start);
    if (word == "true") return {Token::TRUE_T, word};
    if (word == "false") return {Token::FALSE_T, word};
    if (word == "null") return {Token::NULL_T, word};
    throw std::runtime_error("Unknown keyword: " + word);
}

JsonParser::Token JsonParser::Lexer::next() {
    skipWhitespace();
    if (pos >= input.size()) return {Token::END, ""};

    char c = input[pos];
    switch (c) {
        case '{': pos++; return {Token::LBRACE, "{"};
        case '}': pos++; return {Token::RBRACE, "}"};
        case '[': pos++; return {Token::LBRACKET, "["};
        case ']': pos++; return {Token::RBRACKET, "]"};
        case ':': pos++; return {Token::COLON, ":"};
        case ',': pos++; return {Token::COMMA, ","};
        case '"': return readString();
        default:
            if (c == '-' || isdigit(c)) return readNumber();
            if (isalpha(c)) return readKeyword();
            throw std::runtime_error(std::string("Unexpected character: ") + c);
    }
}

float JsonParser::expectNumber(Lexer& lex) {
    Token t = lex.next();
    if (t.type != Token::NUMBER) throw std::runtime_error("Expected number, got: " + t.value);
    return std::stof(t.value);
}

bool JsonParser::expectBool(Lexer& lex) {
    Token t = lex.next();
    if (t.type == Token::TRUE_T) return true;
    if (t.type == Token::FALSE_T) return false;
    throw std::runtime_error("Expected bool, got: " + t.value);
}

std::string JsonParser::expectString(Lexer& lex) {
    Token t = lex.next();
    if (t.type != Token::STRING) throw std::runtime_error("Expected string, got: " + t.value);
    return t.value;
}

void JsonParser::expectToken(Lexer& lex, Token::Type type) {
    Token t = lex.next();
    if (t.type != type) throw std::runtime_error("Unexpected token: " + t.value);
}

Vec3f JsonParser::parseVec3(Lexer& lex) {
    expectToken(lex, Token::LBRACKET);
    float x = expectNumber(lex);
    expectToken(lex, Token::COMMA);
    float y = expectNumber(lex);
    expectToken(lex, Token::COMMA);
    float z = expectNumber(lex);
    expectToken(lex, Token::RBRACKET);
    return Vec3f(x, y, z);
}

void JsonParser::skipValue(Lexer& lex) {
    Token t = lex.next();
    if (t.type == Token::LBRACE) {
        t = lex.next();
        if (t.type == Token::RBRACE) return;
        while (true) {
            expectToken(lex, Token::COLON);
            skipValue(lex);
            t = lex.next();
            if (t.type == Token::RBRACE) return;
            t = lex.next();
        }
    } else if (t.type == Token::LBRACKET) {
        t = lex.next();
        if (t.type == Token::RBRACKET) return;
        int depth = 1;
        while (depth > 0) {
            t = lex.next();
            if (t.type == Token::LBRACKET || t.type == Token::LBRACE) depth++;
            else if (t.type == Token::RBRACKET || t.type == Token::RBRACE) depth--;
        }
    }
}

void JsonParser::parseRender(Lexer& lex, SceneConfig& cfg) {
    expectToken(lex, Token::LBRACE);
    Token t = lex.next();
    while (t.type != Token::RBRACE) {
        std::string key = t.value;
        expectToken(lex, Token::COLON);
        if (key == "width") cfg.render.width = (int)expectNumber(lex);
        else if (key == "height") cfg.render.height = (int)expectNumber(lex);
        else if (key == "samples") cfg.render.spp = (int)expectNumber(lex);
        else if (key == "maxDepth") cfg.render.maxDepth = (int)expectNumber(lex);
        else if (key == "output") cfg.render.outputFile = expectString(lex);
        else if (key == "toneMapping") cfg.render.toneMapping = expectString(lex);
        else if (key == "displayExposure") cfg.render.displayExposure = (float)expectNumber(lex);
        else if (key == "highlightCompression") cfg.render.highlightCompression = (float)expectNumber(lex);
        else if (key == "whitePoint") cfg.render.whitePoint = (float)expectNumber(lex);
        else skipValue(lex);

        t = lex.next();
        if (t.type == Token::COMMA) t = lex.next();
    }
}

void JsonParser::parseCamera(Lexer& lex, SceneConfig& cfg) {
    expectToken(lex, Token::LBRACE);
    Token t = lex.next();
    while (t.type != Token::RBRACE) {
        std::string key = t.value;
        expectToken(lex, Token::COLON);
        if (key == "enabled") cfg.camera.enabled = expectBool(lex);
        else if (key == "position") cfg.camera.position = parseVec3(lex);
        else if (key == "lookAt") cfg.camera.lookAt = parseVec3(lex);
        else if (key == "up") cfg.camera.up = parseVec3(lex);
        else if (key == "fov") cfg.camera.fov = expectNumber(lex);
        else skipValue(lex);

        t = lex.next();
        if (t.type == Token::COMMA) t = lex.next();
    }
}

void JsonParser::parseObjects(Lexer& lex, SceneConfig& cfg) {
    expectToken(lex, Token::LBRACKET);
    Token t = lex.next();
    while (t.type != Token::RBRACKET) {
        SceneConfig::ObjectEntry obj{};
        obj.materialColor = Vec3f(-1);
        obj.scale = 1.0f;
        obj.roughness = -1.0f;
        obj.blendWeight = -1.0f;
        obj.reflectionScale = 1.0f;
        if (t.type != Token::LBRACE) {
            throw std::runtime_error("Expected object entry");
        }
        Token inner = lex.next();
        while (inner.type != Token::RBRACE) {
            std::string key = inner.value;
            expectToken(lex, Token::COLON);
            if (key == "type") obj.type = expectString(lex);
            else if (key == "file") obj.file = expectString(lex);
            else if (key == "position") obj.position = parseVec3(lex);
            else if (key == "scale") obj.scale = expectNumber(lex);
            else if (key == "material") obj.materialType = expectString(lex);
            else if (key == "normalSource") obj.normalSource = expectString(lex);
            else if (key == "color") obj.materialColor = parseVec3(lex);
            else if (key == "roughness") obj.roughness = expectNumber(lex);
            else if (key == "blendWeight") obj.blendWeight = expectNumber(lex);
            else if (key == "reflectionScale") obj.reflectionScale = expectNumber(lex);
            else if (key == "coordinateSystem") {
                const std::string value = expectString(lex);
                if (value == "renderer") {
                    obj.convertFromBlender = false;
                } else if (value == "blender") {
                    obj.convertFromBlender = true;
                } else {
                    throw std::runtime_error("Unsupported coordinateSystem: " + value);
                }
            }
            else skipValue(lex);

            inner = lex.next();
            if (inner.type == Token::COMMA) inner = lex.next();
        }
        cfg.objects.push_back(obj);
        t = lex.next();
        if (t.type == Token::COMMA) t = lex.next();
    }
}

void JsonParser::parseLighting(Lexer& lex, SceneConfig& cfg) {
    expectToken(lex, Token::LBRACE);
    Token t = lex.next();
    while (t.type != Token::RBRACE) {
        std::string key = t.value;
        expectToken(lex, Token::COLON);
        if (key == "areaLight") {
            expectToken(lex, Token::LBRACE);
            Token inner = lex.next();
            while (inner.type != Token::RBRACE) {
                std::string innerKey = inner.value;
                expectToken(lex, Token::COLON);
                if (innerKey == "position") cfg.areaLightConfig.position = parseVec3(lex);
                else if (innerKey == "direction") cfg.areaLightConfig.direction = parseVec3(lex);
                else if (innerKey == "color") cfg.areaLightConfig.color = parseVec3(lex);
                else if (innerKey == "intensity") cfg.areaLightConfig.intensity = expectNumber(lex);
                else if (innerKey == "width") cfg.areaLightConfig.width = expectNumber(lex);
                else if (innerKey == "height") cfg.areaLightConfig.height = expectNumber(lex);
                else skipValue(lex);
                inner = lex.next();
                if (inner.type == Token::COMMA) inner = lex.next();
            }
            cfg.hasAreaLight = true;
        }
        else skipValue(lex);

        t = lex.next();
        if (t.type == Token::COMMA) t = lex.next();
    }
}

void JsonParser::parseWater(Lexer& lex, SceneConfig& cfg) {
    expectToken(lex, Token::LBRACE);
    Token t = lex.next();
    while (t.type != Token::RBRACE) {
        std::string key = t.value;
        expectToken(lex, Token::COLON);
        if (key == "fftEnabled") cfg.water.fftEnabled = expectBool(lex);
        else if (key == "fogDensity") cfg.water.fogDensity = (float)expectNumber(lex);
        else if (key == "reflectionStrength") cfg.water.reflectionStrength = (float)expectNumber(lex);
        else if (key == "normalStrength") cfg.water.normalStrength = (float)expectNumber(lex);
        else if (key == "largeWaveScale") cfg.water.largeWaveScale = (float)expectNumber(lex);
        else if (key == "smallWaveScale") cfg.water.smallWaveScale = (float)expectNumber(lex);
        else if (key == "swell") {
            expectToken(lex, Token::LBRACE);
            Token inner = lex.next();
            while (inner.type != Token::RBRACE) {
                std::string innerKey = inner.value;
                expectToken(lex, Token::COLON);
                if (innerKey == "resolution") cfg.water.swell.resolution = (int)expectNumber(lex);
                else if (innerKey == "patchLength") cfg.water.swell.patchLength = (float)expectNumber(lex);
                else if (innerKey == "windSpeed") cfg.water.swell.windSpeed = (float)expectNumber(lex);
                else if (innerKey == "windDirection") cfg.water.swell.windDirection = parseVec3(lex);
                else if (innerKey == "waveHeight") cfg.water.swell.waveHeight = (float)expectNumber(lex);
                else if (innerKey == "time") cfg.water.swell.time = (float)expectNumber(lex);
                else skipValue(lex);
                inner = lex.next();
                if (inner.type == Token::COMMA) inner = lex.next();
            }
        }
        else if (key == "ripple") {
            expectToken(lex, Token::LBRACE);
            Token inner = lex.next();
            while (inner.type != Token::RBRACE) {
                std::string innerKey = inner.value;
                expectToken(lex, Token::COLON);
                if (innerKey == "resolution") cfg.water.ripple.resolution = (int)expectNumber(lex);
                else if (innerKey == "patchLength") cfg.water.ripple.patchLength = (float)expectNumber(lex);
                else if (innerKey == "windSpeed") cfg.water.ripple.windSpeed = (float)expectNumber(lex);
                else if (innerKey == "windDirection") cfg.water.ripple.windDirection = parseVec3(lex);
                else if (innerKey == "waveHeight") cfg.water.ripple.waveHeight = (float)expectNumber(lex);
                else if (innerKey == "time") cfg.water.ripple.time = (float)expectNumber(lex);
                else skipValue(lex);
                inner = lex.next();
                if (inner.type == Token::COMMA) inner = lex.next();
            }
        }
        else skipValue(lex);

        t = lex.next();
        if (t.type == Token::COMMA) t = lex.next();
    }
}

void JsonParser::parseSky(Lexer& lex, SceneConfig& cfg) {
    expectToken(lex, Token::LBRACE);
    Token t = lex.next();
    while (t.type != Token::RBRACE) {
        std::string key = t.value;
        expectToken(lex, Token::COLON);
        if      (key == "enabled")           cfg.sky.enabled          = expectBool(lex);
        else if (key == "horizonColor")      cfg.sky.horizonColor     = parseVec3(lex);
        else if (key == "sunDirection")      cfg.sky.sunDirection     = parseVec3(lex);
        else if (key == "sunDiskColor")      cfg.sky.sunDiskColor     = parseVec3(lex);
        else if (key == "sunGlowColor")      cfg.sky.sunGlowColor     = parseVec3(lex);
        else if (key == "sunAngularRadius")  cfg.sky.sunAngularRadius = expectNumber(lex);
        else if (key == "sunEdgeSoftness")   cfg.sky.sunEdgeSoftness  = expectNumber(lex);
        else if (key == "sunIntensity")      cfg.sky.sunIntensity     = expectNumber(lex);
        else if (key == "sunDiskIntensity")  cfg.sky.sunDiskIntensity = expectNumber(lex);
        else if (key == "skyIntensity")      cfg.sky.skyIntensity     = expectNumber(lex);
        else if (key == "horizonWarmth")     cfg.sky.horizonWarmth    = expectNumber(lex);
        else if (key == "sunsetGradientStrength") cfg.sky.sunsetGradientStrength = expectNumber(lex);
        else if (key == "cloudsEnabled")     cfg.sky.cloudsEnabled    = expectBool(lex);
        else if (key == "cloudScale")        cfg.sky.cloudScale       = expectNumber(lex);
        else if (key == "cloudThreshold")    cfg.sky.cloudThreshold   = expectNumber(lex);
        else if (key == "cloudSoftness")     cfg.sky.cloudSoftness    = expectNumber(lex);
        else if (key == "cloudOpacity")      cfg.sky.cloudOpacity     = expectNumber(lex);
        else if (key == "cloudDarkColor")    cfg.sky.cloudDarkColor   = parseVec3(lex);
        else if (key == "cloudWarmColor")    cfg.sky.cloudWarmColor   = parseVec3(lex);
        else if (key == "cloudAdaptiveScaleEnabled") cfg.sky.cloudAdaptiveScaleEnabled = expectBool(lex);
        else if (key == "cloudAdaptiveMinScale")     cfg.sky.cloudAdaptiveMinScale     = expectNumber(lex);
        else if (key == "cloudAdaptiveMaxScale")     cfg.sky.cloudAdaptiveMaxScale     = expectNumber(lex);
        else if (key == "cloudSunEdgeIntensity")     cfg.sky.cloudSunEdgeIntensity     = expectNumber(lex);
        else if (key == "cloudSunEdgePower")         cfg.sky.cloudSunEdgePower         = expectNumber(lex);
        else if (key == "cloudSunFocusPower")        cfg.sky.cloudSunFocusPower        = expectNumber(lex);
        else skipValue(lex);

        t = lex.next();
        if (t.type == Token::COMMA) t = lex.next();
    }
}

void JsonParser::parseArtTricks(Lexer& lex, SceneConfig& cfg) {
    expectToken(lex, Token::LBRACE);
    Token t = lex.next();
    while (t.type != Token::RBRACE) {
        std::string key = t.value;
        expectToken(lex, Token::COLON);
        if (key == "enabled") {
            cfg.artTricks.enabled = expectBool(lex);
        } else if (key == "ambientStrength") {
            cfg.artTricks.ambientStrength = expectNumber(lex);
        } else if (key == "environmentDiffuseStrength") {
            cfg.artTricks.environmentDiffuseStrength = expectNumber(lex);
        } else if (key == "upperSkyFillStrength") {
            cfg.artTricks.upperSkyFillStrength = expectNumber(lex);
        } else if (key == "upperSkyFillColor") {
            cfg.artTricks.upperSkyFillColor = parseVec3(lex);
        } else if (key == "horizonFillStrength") {
            cfg.artTricks.horizonFillStrength = expectNumber(lex);
        } else if (key == "bounceStrength") {
            cfg.artTricks.bounceStrength = expectNumber(lex);
        } else if (key == "bounceColor") {
            cfg.artTricks.bounceColor = parseVec3(lex);
        } else if (key == "bounceDirection") {
            cfg.artTricks.bounceDirection = parseVec3(lex);
        } else if (key == "bounceFalloff") {
            cfg.artTricks.bounceFalloff = expectNumber(lex);
        } else if (key == "bounceMaxContribution") {
            cfg.artTricks.bounceMaxContribution = expectNumber(lex);
        } else if (key == "shadowLift") {
            cfg.artTricks.shadowLift = expectNumber(lex);
        } else if (key == "backLightScale") {
            cfg.artTricks.backLightScale = expectNumber(lex);
        } else if (key == "fogColorScale") {
            cfg.artTricks.fogColorScale = expectNumber(lex);
        } else if (key == "mirrorBlendEnabled") {
            cfg.artTricks.mirrorBlendEnabled = expectBool(lex);
        } else if (key == "mirrorBlendWeight") {
            cfg.artTricks.mirrorBlendWeight = expectNumber(lex);
        } else if (key == "clearcoatEnvReflectionScale") {
            cfg.artTricks.clearcoatEnvReflectionScale = expectNumber(lex);
        } else if (key == "blendAmbientTint") {
            cfg.artTricks.blendAmbientTint = parseVec3(lex);
        } else if (key == "diffuseAmbientTint") {
            cfg.artTricks.diffuseAmbientTint = parseVec3(lex);
        } else if (key == "upperSkyFillScale") {
            cfg.artTricks.upperSkyFillScale = expectNumber(lex);
        } else if (key == "blendSkyFillScale") {
            cfg.artTricks.blendSkyFillScale = expectNumber(lex);
        } else if (key == "diffuseSkyFillScale") {
            cfg.artTricks.diffuseSkyFillScale = expectNumber(lex);
        } else if (key == "clearcoatF0") {
            cfg.artTricks.clearcoatF0 = expectNumber(lex);
        } else if (key == "clearcoatStrengthScale") {
            cfg.artTricks.clearcoatStrengthScale = expectNumber(lex);
        } else {
            skipValue(lex);
        }

        t = lex.next();
        if (t.type == Token::COMMA) t = lex.next();
    }
}

SceneConfig JsonParser::parse(const std::string& filename) {
    SceneConfig cfg{};

    // Load shared locked settings first, then let the main scene file override them.
    std::filesystem::path scenePath = resolveInputPath(filename);
    std::filesystem::path lockedPath = scenePath.parent_path() / "locked_settings.json";
    if (std::filesystem::exists(lockedPath)) {
        parseInto(lockedPath.string(), cfg);
    }

    parseInto(filename, cfg);
    return cfg;
}

void JsonParser::parseInto(const std::string& filename, SceneConfig& cfg) {
    std::filesystem::path resolvedFilename = resolveInputPath(filename);
    std::ifstream file(resolvedFilename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filename);
    }
    std::filesystem::path baseDir = resolvedFilename.parent_path();

    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    Lexer lex(content);

    expectToken(lex, Token::LBRACE);

    Token t = lex.next();
    while (t.type != Token::RBRACE && t.type != Token::END) {
        std::string key = t.value;
        expectToken(lex, Token::COLON);

        if (key == "render") parseRender(lex, cfg);
        else if (key == "camera") parseCamera(lex, cfg);
        else if (key == "objects") parseObjects(lex, cfg);
        else if (key == "lighting") parseLighting(lex, cfg);
        else if (key == "water") parseWater(lex, cfg);
        else if (key == "sky") parseSky(lex, cfg);
        else if (key == "artTricks") parseArtTricks(lex, cfg);
        else skipValue(lex);

        t = lex.next();
        if (t.type == Token::COMMA) t = lex.next();
    }

    for (auto& obj : cfg.objects) {
        if (!obj.file.empty()) {
            obj.file = resolveAssetPath(baseDir, obj.file);
        }
    }

}

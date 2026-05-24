#include "JsonParser.h"
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

std::string resolveRelativeTo(const std::filesystem::path& baseDir, const std::string& value) {
    std::filesystem::path path(value);
    if (path.is_relative()) {
        return (baseDir / path).string();
    }
    return path.string();
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

// ===== Lexer =====

void JsonParser::Lexer::skipWhitespace() {
    while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t' ||
           input[pos] == '\n' || input[pos] == '\r')) {
        pos++;
    }
}

JsonParser::Token JsonParser::Lexer::readString() {
    pos++; // skip opening "
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
    pos++; // skip closing "
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

// ===== Parser helpers =====

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
        // Skip object
        t = lex.next();
        if (t.type == Token::RBRACE) return;
        while (true) {
            // key
            expectToken(lex, Token::COLON);
            skipValue(lex);
            t = lex.next();
            if (t.type == Token::RBRACE) return;
            // comma, then next key
            t = lex.next(); // key string
        }
    } else if (t.type == Token::LBRACKET) {
        t = lex.next();
        if (t.type == Token::RBRACKET) return;
        // We already consumed first element's first token, need to handle
        // For simplicity, re-parse
        int depth = 1;
        while (depth > 0) {
            t = lex.next();
            if (t.type == Token::LBRACKET || t.type == Token::LBRACE) depth++;
            else if (t.type == Token::RBRACKET || t.type == Token::RBRACE) depth--;
        }
    }
    // STRING, NUMBER, TRUE, FALSE, NULL are already consumed
}

// ===== Section parsers =====

void JsonParser::parseRender(Lexer& lex, SceneConfig& cfg) {
    expectToken(lex, Token::LBRACE);
    Token t = lex.next();
    while (t.type != Token::RBRACE) {
        std::string key = t.value;
        expectToken(lex, Token::COLON);
        if (key == "width") cfg.width = (int)expectNumber(lex);
        else if (key == "height") cfg.height = (int)expectNumber(lex);
        else if (key == "samples") cfg.spp = (int)expectNumber(lex);
        else if (key == "maxDepth") cfg.maxDepth = (int)expectNumber(lex);
        else if (key == "output") cfg.outputFile = expectString(lex);
        else if (key == "debug" || key == "debugMode") cfg.debugMode = expectBool(lex);
        else if (key == "materialDebug") cfg.materialDebug = expectString(lex);
        else if (key == "renderMode") cfg.renderMode = expectString(lex);
        else if (key == "toneMapping") cfg.toneMapping = expectString(lex);
        else if (key == "displayExposure" || key == "exposure") cfg.displayExposure = (float)expectNumber(lex);
        else if (key == "highlightCompression") cfg.highlightCompression = (float)expectNumber(lex);
        else if (key == "whitePoint") cfg.whitePoint = (float)expectNumber(lex);
        else if (key == "aircraftClearcoatStrength") cfg.aircraftClearcoatStrength = (float)expectNumber(lex);
        else if (key == "aircraftClearcoatEnvBoost") cfg.aircraftClearcoatEnvBoost = (float)expectNumber(lex);
        else if (key == "aircraftClearcoatF0") cfg.aircraftClearcoatF0 = (float)expectNumber(lex);
        else if (key == "aircraftMirrorDebug") cfg.aircraftMirrorDebug = expectBool(lex);
        else if (key == "waterReflectionStrength") cfg.waterReflectionStrength = (float)expectNumber(lex);
        else if (key == "waterSunReflectionStrength") cfg.waterSunReflectionStrength = (float)expectNumber(lex);
        else if (key == "waterWarmth") cfg.waterWarmth = (float)expectNumber(lex);
        else if (key == "waterRoughness") cfg.waterRoughness = (float)expectNumber(lex);
        else if (key == "waterNormalStrength") cfg.waterNormalStrength = (float)expectNumber(lex);
        else if (key == "skyFillStrength") cfg.skyFillStrength = (float)expectNumber(lex);
        else if (key == "horizonFillStrength") cfg.horizonFillStrength = (float)expectNumber(lex);
        else if (key == "waterBounceStrength") cfg.waterBounceStrength = (float)expectNumber(lex);
        else if (key == "waterBounceColor") cfg.waterBounceColor = parseVec3(lex);
        else if (key == "waterBounceDirection") cfg.waterBounceDirection = parseVec3(lex);
        else if (key == "waterBounceFalloff") cfg.waterBounceFalloff = (float)expectNumber(lex);
        else if (key == "waterBounceMaxContribution") cfg.waterBounceMaxContribution = (float)expectNumber(lex);
        else if (key == "upperSkyFillColor") cfg.upperSkyFillColor = parseVec3(lex);
        else if (key == "environmentDiffuseStrength") cfg.environmentDiffuseStrength = (float)expectNumber(lex);
        else if (key == "environmentReflectionStrength") cfg.environmentReflectionStrength = (float)expectNumber(lex);
        else if (key == "ambientStrength") cfg.ambientStrength = (float)expectNumber(lex);
        else if (key == "shadowLift") cfg.shadowLift = (float)expectNumber(lex);
        else if (key == "waterReflectionFloor") cfg.waterReflectionFloor = (float)expectNumber(lex);
        else if (key == "waterFresnelBias") cfg.waterFresnelBias = (float)expectNumber(lex);
        else if (key == "waterRefractionWeight") cfg.waterRefractionWeight = (float)expectNumber(lex);
        else if (key == "waterBaseAbsorption") cfg.waterBaseAbsorption = (float)expectNumber(lex);
        else if (key == "waterForegroundDarkening") cfg.waterForegroundDarkening = (float)expectNumber(lex);
        else if (key == "waterLargeWaveScale") cfg.waterLargeWaveScale = (float)expectNumber(lex);
        else if (key == "waterSmallWaveScale") cfg.waterSmallWaveScale = (float)expectNumber(lex);
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
        if (key == "position") cfg.cameraPos = parseVec3(lex);
        else if (key == "lookAt") cfg.cameraLookAt = parseVec3(lex);
        else if (key == "up" || key == "cameraUp") cfg.cameraUp = parseVec3(lex);
        else if (key == "right" || key == "cameraRight") cfg.cameraRight = parseVec3(lex);
        else if (key == "fov") cfg.fov = expectNumber(lex);
        else skipValue(lex);

        t = lex.next();
        if (t.type == Token::COMMA) t = lex.next();
    }
}

void JsonParser::parseObjects(Lexer& lex, SceneConfig& cfg) {
    expectToken(lex, Token::LBRACKET);
    Token t = lex.next();
    while (t.type != Token::RBRACKET) {
        SceneConfig::ObjectEntry obj;
        // t should be LBRACE — push back by parsing object starting from here
        // Actually parseObject expects to read LBRACE itself, so we need to handle
        if (t.type == Token::LBRACE || t.type == Token::COMMA) {
            if (t.type == Token::COMMA) {
                // next should be LBRACE
            }
        }
        // Simpler: re-structure
        // We already consumed first token
        if (t.type != Token::RBRACKET) {
            // Parse object inline
            Token inner = lex.next();
            while (inner.type != Token::RBRACE) {
                std::string key = inner.value;
                expectToken(lex, Token::COLON);
                if (key == "type") obj.type = expectString(lex);
                else if (key == "file") obj.file = expectString(lex);
                else if (key == "position") obj.position = parseVec3(lex);
                else if (key == "rotation") obj.rotation = parseVec3(lex);
                else if (key == "scale") obj.scale = expectNumber(lex);
                else if (key == "material") obj.materialType = expectString(lex);
                else if (key == "color") obj.materialColor = parseVec3(lex);
                else if (key == "roughness") obj.roughness = expectNumber(lex);
                else if (key == "metallic") obj.metallic = expectNumber(lex);
                else if (key == "glossyWeight") obj.glossyWeight = expectNumber(lex);
                else if (key == "specularBoost") obj.specularBoost = expectNumber(lex);
                else if (key == "ior") obj.ior = expectNumber(lex);
                else if (key == "coordinateSystem") obj.convertFromBlender = expectString(lex) == "blender";
                else if (key == "blenderCoordinates") obj.convertFromBlender = expectBool(lex);
                else skipValue(lex);

                inner = lex.next();
                if (inner.type == Token::COMMA) inner = lex.next();
            }
            cfg.objects.push_back(obj);
        }
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

void JsonParser::parseSky(Lexer& lex, SceneConfig& cfg) {
    expectToken(lex, Token::LBRACE);
    Token t = lex.next();
    while (t.type != Token::RBRACE) {
        std::string key = t.value;
        expectToken(lex, Token::COLON);
        if      (key == "enabled")           cfg.sky.enabled          = expectBool(lex);
        else if (key == "topColor")          cfg.sky.topColor         = parseVec3(lex);
        else if (key == "horizonColor")      cfg.sky.horizonColor     = parseVec3(lex);
        else if (key == "bottomColor")       cfg.sky.bottomColor      = parseVec3(lex);
        else if (key == "sunDirection")      cfg.sky.sunDirection     = parseVec3(lex);
        else if (key == "sunDiskColor")      cfg.sky.sunDiskColor     = parseVec3(lex);
        else if (key == "sunGlowColor")      cfg.sky.sunGlowColor     = parseVec3(lex);
        else if (key == "sunDiskPower")      cfg.sky.sunDiskPower     = expectNumber(lex);
        else if (key == "sunGlowPower")      cfg.sky.sunGlowPower     = expectNumber(lex);
        else if (key == "sunAngularRadius")  cfg.sky.sunAngularRadius = expectNumber(lex);
        else if (key == "sunEdgeSoftness")   cfg.sky.sunEdgeSoftness  = expectNumber(lex);
        else if (key == "sunIntensity")      cfg.sky.sunIntensity     = expectNumber(lex);
        else if (key == "sunDiskIntensity")  cfg.sky.sunDiskIntensity = expectNumber(lex);
        else if (key == "sunGlowIntensity")  cfg.sky.sunGlowIntensity = expectNumber(lex);
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
        // Legacy keys for backward compatibility
        else if (key == "top")    cfg.sky.topColor    = parseVec3(lex);
        else if (key == "bottom") cfg.sky.bottomColor = parseVec3(lex);
        else skipValue(lex);

        t = lex.next();
        if (t.type == Token::COMMA) t = lex.next();
    }
}

void JsonParser::parseMistVolume(Lexer& lex, SceneConfig::MistVolumeConfig& vol) {
    expectToken(lex, Token::LBRACE);
    Token t = lex.next();
    while (t.type != Token::RBRACE) {
        std::string key = t.value;
        expectToken(lex, Token::COLON);
        if      (key == "enabled")            vol.enabled            = expectBool(lex);
        else if (key == "center")             vol.center             = parseVec3(lex);
        else if (key == "size")               vol.size               = parseVec3(lex);
        else if (key == "density")            vol.density            = expectNumber(lex);
        else if (key == "absorption")         vol.absorption         = expectNumber(lex);
        else if (key == "scatteringStrength")  vol.scatteringStrength = expectNumber(lex);
        else if (key == "coverage")           vol.coverage           = expectNumber(lex);
        else if (key == "softness")           vol.softness           = expectNumber(lex);
        else if (key == "noiseScale")         vol.noiseScale         = expectNumber(lex);
        else if (key == "noiseOffset")        vol.noiseOffset        = parseVec3(lex);
        else if (key == "heightFalloff")      vol.heightFalloff      = expectNumber(lex);
        else if (key == "edgeSoftness")       vol.edgeSoftness       = expectNumber(lex);
        else if (key == "coolAmbientColor")   vol.coolAmbientColor   = parseVec3(lex);
        else if (key == "warmSunColor")       vol.warmSunColor       = parseVec3(lex);
        else if (key == "sunRimStrength")     vol.sunRimStrength     = expectNumber(lex);
        else if (key == "maxAlpha")           vol.maxAlpha           = expectNumber(lex);
        else if (key == "marchSteps")         vol.marchSteps         = (int)expectNumber(lex);
        else if (key == "shadowSteps")        vol.shadowSteps        = (int)expectNumber(lex);
        else skipValue(lex);

        t = lex.next();
        if (t.type == Token::COMMA) t = lex.next();
    }
}

// ===== Main parse =====

SceneConfig JsonParser::parse(const std::string& filename) {
    SceneConfig cfg;
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

    expectToken(lex, Token::LBRACE); // root {

    Token t = lex.next();
    while (t.type != Token::RBRACE && t.type != Token::END) {
        std::string key = t.value;
        expectToken(lex, Token::COLON);

        if (key == "render") parseRender(lex, cfg);
        else if (key == "camera") parseCamera(lex, cfg);
        else if (key == "objects") parseObjects(lex, cfg);
        else if (key == "lighting") parseLighting(lex, cfg);
        else if (key == "sky") parseSky(lex, cfg);
        else if (key == "leftMist")  parseMistVolume(lex, cfg.leftMist);
        else if (key == "rightMist") parseMistVolume(lex, cfg.rightMist);
        else skipValue(lex);

        t = lex.next();
        if (t.type == Token::COMMA) t = lex.next();
    }

    if (cfg.outputFile != "output.ppm") {
        cfg.outputFile = resolveRelativeTo(baseDir, cfg.outputFile);
    }

    for (auto& obj : cfg.objects) {
        if (!obj.file.empty()) {
            obj.file = resolveAssetPath(baseDir, obj.file);
        }
    }

}

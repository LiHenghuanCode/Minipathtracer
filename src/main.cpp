#include "scene/Scene.h"
#include "core/Renderer.h"
#include "scene/JsonParser.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string configFile = "config/scene.json";
    if (argc > 1) {
        configFile = argv[1];
    }

    std::cout << "=== Path Tracer ===" << std::endl;
    std::cout << "Config: " << configFile << std::endl;

    try {
        SceneConfig config = JsonParser::parse(configFile);

        std::cout << "Resolution: " << config.render.width << "x" << config.render.height << std::endl;
        std::cout << "SPP: " << config.render.spp << std::endl;
        std::cout << "Max Depth: " << config.render.maxDepth << std::endl;
        std::cout << "Objects: " << config.objects.size() << std::endl;

        Scene scene;
        scene.loadFromConfig(config);

        Renderer renderer;
        renderer.render(scene);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

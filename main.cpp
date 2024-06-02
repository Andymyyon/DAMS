#include <SFML/Graphics.hpp>
#include <boost/polygon/voronoi.hpp>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <string>
#include <cmath>

using namespace sf;
using namespace boost::polygon;

struct Airport {
    std::string name;
    Vector2f coordinates;
    bool overloaded;
    int aircraft_count;  // Nombre d'avions à charge
};

struct Point {
    int x, y;

    Point(int x = 0, int y = 0) : x(x), y(y) {}
};

// Define the necessary traits for using Point with Boost.Polygon
namespace boost { namespace polygon {
    template <>
    struct geometry_concept<Point> { typedef point_concept type; };

    template <>
    struct point_traits<Point> {
        typedef int coordinate_type;

        static inline coordinate_type get(const Point& point, orientation_2d orient) {
            return (orient == HORIZONTAL) ? point.x : point.y;
        }
    };
}}

std::vector<Airport> readAirportsFromCSV(const std::string& filename) {
    std::vector<Airport> airports;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return airports;
    }

    std::string line;
    // Skip the header
    if (!std::getline(file, line)) {
        std::cerr << "Failed to read header from " << filename << std::endl;
        return airports;
    }

    while (std::getline(file, line)) {
        std::istringstream s(line);
        std::string name, lat, lon, overloaded, aircraft_count;
        if (!(std::getline(s, name, ',') && std::getline(s, lat, ',') && std::getline(s, lon, ',') && std::getline(s, overloaded, ',') && std::getline(s, aircraft_count, ','))) {
            std::cerr << "Failed to parse line: " << line << std::endl;
            continue;
        }

        float latitude = std::stof(lat);
        float longitude = std::stof(lon);
        bool isOverloaded = (std::stof(overloaded) == 1.0);
        int aircraftCount = std::stoi(aircraft_count);

        airports.push_back({name, {latitude, longitude}, isOverloaded, aircraftCount});
    }

    return airports;
}

struct VoronoiCell {
    std::vector<Vector2f> vertices;
    bool overloaded;
};

std::vector<VoronoiCell> computeVoronoiCells(const std::vector<Airport>& airports, float scaleX, float scaleY, int width, int height, std::vector<std::pair<Vector2f, Vector2f>>& edges) {
    std::vector<VoronoiCell> voronoiCells;

    // Convert airports to points
    std::vector<Point> points;
    for (const auto& airport : airports) {
        points.emplace_back(static_cast<int>(airport.coordinates.x * scaleX), static_cast<int>(airport.coordinates.y * scaleY));
    }

    // Build the Voronoi diagram
    voronoi_diagram<double> vd;
    construct_voronoi(points.begin(), points.end(), &vd);

    // Helper function to extend edge to the bounding box
    auto extendEdgeToBoundingBox = [&](double x0, double y0, double x1, double y1) {
        double dx = x1 - x0;
        double dy = y1 - y0;
        double t_min = 0.0;
        double t_max = 1.0;

        if (dx != 0.0) {
            if (dx > 0) {
                t_max = std::min(t_max, (width - x0) / dx);
                t_min = std::max(t_min, (0 - x0) / dx);
            } else {
                t_max = std::min(t_max, (0 - x0) / dx);
                t_min = std::max(t_min, (width - x0) / dx);
            }
        }

        if (dy != 0.0) {
            if (dy > 0) {
                t_max = std::min(t_max, (height - y0) / dy);
                t_min = std::max(t_min, (0 - y0) / dy);
            } else {
                t_max = std::min(t_max, (0 - y0) / dy);
                t_min = std::max(t_min, (height - y0) / dy);
            }
        }

        return std::make_pair(Vector2f(static_cast<float>(x0 + t_min * dx), static_cast<float>(y0 + t_min * dy)),
                              Vector2f(static_cast<float>(x0 + t_max * dx), static_cast<float>(y0 + t_max * dy)));
    };

    // Iterate over Voronoi cells
    for (auto it = vd.cells().begin(); it != vd.cells().end(); ++it) {
        const voronoi_diagram<double>::cell_type& cell = *it;
        const voronoi_diagram<double>::edge_type* edge = cell.incident_edge();

        if (!edge) continue;

        VoronoiCell voronoiCell;
        voronoiCell.overloaded = airports[it->source_index()].overloaded;
        bool has_vertices = false;

        do {
            if (edge->is_primary()) {
                const voronoi_diagram<double>::vertex_type* v0 = edge->vertex0();
                const voronoi_diagram<double>::vertex_type* v1 = edge->vertex1();

                if (v0 && v1) {
                    Vector2f p0(static_cast<float>(v0->x() / scaleX), static_cast<float>(v0->y() / scaleY));
                    Vector2f p1(static_cast<float>(v1->x() / scaleX), static_cast<float>(v1->y() / scaleY));
                    voronoiCell.vertices.push_back(p0);
                    voronoiCell.vertices.push_back(p1);
                    edges.push_back(std::make_pair(p0, p1));
                    has_vertices = true;
                } else {
                    if (v0 || v1) {
                        double x0 = v0 ? v0->x() : v1->x();
                        double y0 = v0 ? v0->y() : v1->y();
                        double x1 = v1 ? v1->x() : v0->x();
                        double y1 = v1 ? v1->y() : v0->y();
                        auto bboxEdges = extendEdgeToBoundingBox(x0, y0, x1, y1);
                        voronoiCell.vertices.push_back(bboxEdges.first);
                        voronoiCell.vertices.push_back(bboxEdges.second);
                        edges.push_back(bboxEdges);
                        has_vertices = true;
                    }
                }
            }

            edge = edge->next();
        } while (edge != cell.incident_edge());

        // Only add cells that have vertices and are associated with a point
        if (has_vertices && cell.contains_point()) {
            voronoiCells.push_back(voronoiCell);
        }
    }

    return voronoiCells;
}

int main(int argc, char const* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mode>" << std::endl;
        std::cerr << "Modes: 'unoptimized', 'optimized'" << std::endl;
        return EXIT_FAILURE;
    }

    std::string mode = argv[1];

    const int WIDTH = 1200, HEIGHT = 800;
    const float MARGIN = 0.05;  // 5% margin

    ContextSettings settings;
    settings.antialiasingLevel = 8;
    RenderWindow window(VideoMode(WIDTH, HEIGHT), "Voronoi Diagram", Style::Close | Style::Titlebar, settings);

    std::vector<Airport> airports;
    if (mode == "unoptimized") {
        airports = readAirportsFromCSV("data/airports.csv");
        if (airports.empty()) {
            std::cerr << "No airports loaded from CSV" << std::endl;
            return EXIT_FAILURE;
        }
    } else if (mode == "optimized") {
        airports = readAirportsFromCSV("data/adjusted_airports.csv");
        if (airports.empty()) {
            std::cerr << "No optimized voronoi diagram loaded from CSV" << std::endl;
            return EXIT_FAILURE;
        }
    } else {
        std::cerr << "Invalid mode: " << mode << std::endl;
        return EXIT_FAILURE;
    }

    float minLat = airports[0].coordinates.x, maxLat = airports[0].coordinates.x;
    float minLon = airports[0].coordinates.y, maxLon = airports[0].coordinates.y;
    for (const auto& airport : airports) {
        if (airport.coordinates.x < minLat) minLat = airport.coordinates.x;
        if (airport.coordinates.x > maxLat) maxLat = airport.coordinates.x;
        if (airport.coordinates.y < minLon) minLon = airport.coordinates.y;
        if (airport.coordinates.y > maxLon) maxLon = airport.coordinates.y;
    }

    float rangeLat = maxLat - minLat;
    float rangeLon = maxLon - minLon;

    float scaleX = (WIDTH / (rangeLat * (1 + 2 * MARGIN)));
    float scaleY = (HEIGHT / (rangeLon * (1 + 2 * MARGIN)));

    for (auto& airport : airports) {
        airport.coordinates.x = ((airport.coordinates.x - minLat) / rangeLat) * WIDTH * (1 - 2 * MARGIN) + WIDTH * MARGIN;
        airport.coordinates.y = ((airport.coordinates.y - minLon) / rangeLon) * HEIGHT * (1 - 2 * MARGIN) + HEIGHT * MARGIN;
    }

    std::vector<std::pair<Vector2f, Vector2f>> edges;
    std::vector<VoronoiCell> cells = computeVoronoiCells(airports, scaleX, scaleY, WIDTH, HEIGHT, edges);

    RenderTexture renderTexture;
    if (!renderTexture.create(WIDTH, HEIGHT)) {
        std::cerr << "Failed to create render texture" << std::endl;
        return EXIT_FAILURE;
    }

    Sprite sprite(renderTexture.getTexture());
    sprite.setTextureRect(IntRect(0, 0, WIDTH, HEIGHT));

    // Create circles for points
    std::vector<CircleShape> circles;
    float radius = 4;
    CircleShape tempPoint(radius, 100);
    tempPoint.setOrigin(radius, radius);
    tempPoint.setFillColor(Color::Black);  // Points are always black

    for (const auto& airport : airports) {
        tempPoint.setPosition(airport.coordinates);
        circles.push_back(tempPoint);
    }

    Font font;
    if (!font.loadFromFile("fonts/arial.ttf")) {  // Assurez-vous que le fichier de police arial.ttf est présent dans le répertoire fonts
        std::cerr << "Failed to load font" << std::endl;
        return EXIT_FAILURE;
    }

    std::vector<Text> airportTexts;
    for (const auto& airport : airports) {
        Text airportNameText;
        airportNameText.setFont(font);
        airportNameText.setCharacterSize(15);
        airportNameText.setFillColor(Color::Black);
        airportNameText.setString(airport.name + " (" + std::to_string(airport.aircraft_count) + ")");
        airportNameText.setPosition(airport.coordinates.x + 10, airport.coordinates.y);
        airportTexts.push_back(airportNameText);
    }

    while (window.isOpen()) {
        Event event;
        while (window.pollEvent(event)) {
            if (event.type == Event::Closed)
                window.close();
        }

        if (Keyboard::isKeyPressed(Keyboard::Escape))
            window.close();

        renderTexture.clear(Color::White);

        // Draw Voronoi cells
        for (const auto& cell : cells) {
            sf::ConvexShape polygon;
            polygon.setPointCount(cell.vertices.size());
            for (size_t i = 0; i < cell.vertices.size(); ++i) {
                polygon.setPoint(i, cell.vertices[i]);
            }
            polygon.setFillColor(cell.overloaded ? Color::Red : Color::Green);
            polygon.setOutlineThickness(1);
            polygon.setOutlineColor(Color::Black);
            renderTexture.draw(polygon);
        }

        // Draw Voronoi edges in blue
        for (const auto& edge : edges) {
            sf::Vertex line[] = {
                sf::Vertex(edge.first, sf::Color::Blue),
                sf::Vertex(edge.second, sf::Color::Blue)
            };
            renderTexture.draw(line, 2, sf::Lines);
        }

        // Draw points
        for (const auto& circle : circles) {
            renderTexture.draw(circle);
        }

        // Draw airport names and aircraft counts
        for (const auto& text : airportTexts) {
            renderTexture.draw(text);
        }

        renderTexture.display();

        window.clear(Color::White);
        window.draw(Sprite(renderTexture.getTexture()));

        window.display();
    }

    return 0;
}

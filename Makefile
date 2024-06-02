# Compilateur et options
CXX = g++
CXXFLAGS = -std=c++14 -Wall -Wextra

# Bibliothèques SFML
LDFLAGS = -lsfml-graphics -lsfml-window -lsfml-system

# Nom de l'exécutable
TARGET = VoronoiDiagram

# Fichiers source
SRCS = main.cpp

# Règle par défaut
all: $(TARGET)

# Règle pour créer l'exécutable
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

# Règle pour nettoyer les fichiers de construction
clean:
	rm -f $(TARGET) *.o

# Règle pour exécuter le programme
run: $(TARGET)
	./$(TARGET) unoptimized

runopt: $(TARGET)
	./$(TARGET) optimized

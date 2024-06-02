import csv
import os
import requests
import subprocess
import time
import numpy as np

MAX_ITERATIONS = 10  # Nombre maximum d'itérations pour l'ajustement
ADJUSTMENT_STEP = 0.1  # Pas de déplacement pour chaque ajustement
RELATIVE_THRESHOLD = 1.3  # Seuil relatif pour définir la surcharge (130% de la charge moyenne)

def read_airports_from_csv(filename):
    airports = {}
    with open(filename, 'r') as file:
        reader = csv.reader(file)
        next(reader)  # Skip header
        for row in reader:
            name, lat, lon = row[:3]  # Only read the first three columns
            airports[name] = (name, float(lat), float(lon))
    return airports

def fetch_aircraft_data(lat, lon, distance=25):
    url = f"https://adsbx-flight-sim-traffic.p.rapidapi.com/api/aircraft/json/lat/{lat}/lon/{lon}/dist/{distance}/"
    headers = {
        "X-RapidAPI-Key": "your own key as mine reached the monthly limit anyways",
        "X-RapidAPI-Host": "adsbx-flight-sim-traffic.p.rapidapi.com"
    }
    response = requests.get(url, headers=headers)
    response.raise_for_status()  # Will raise an HTTPError if the HTTP request returned an unsuccessful status code
    return response.json()

def save_aircraft_data_to_csv(aircraft_data, filename):
    with open(filename, 'w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['airport', 'icao', 'latitude', 'longitude'])
        for airport, aircrafts in aircraft_data.items():
            if aircrafts:
                for aircraft in aircrafts:
                    writer.writerow([
                        airport,
                        aircraft.get('icao', ''),
                        aircraft.get('lat', ''),
                        aircraft.get('lon', '')
                    ])

def read_aircraft_data_from_csv(filename):
    aircraft_data = {}
    with open(filename, 'r') as file:
        reader = csv.reader(file)
        next(reader)  # Skip header
        for row in reader:
            airport, icao, lat, lon = row
            if airport not in aircraft_data:
                aircraft_data[airport] = []
            aircraft_data[airport].append({
                'icao': icao,
                'lat': float(lat),
                'lon': float(lon)
            })
    return aircraft_data

def calculate_airport_load(airports, aircraft_data):
    aircraft_to_airports = {}

    for airport, aircrafts in aircraft_data.items():
        for aircraft in aircrafts:
            icao = aircraft['icao']
            lat = aircraft['lat']
            lon = aircraft['lon']
            if icao not in aircraft_to_airports:
                aircraft_to_airports[icao] = []
            aircraft_to_airports[icao].append((airport, lat, lon))

    airport_load = {airport: 0 for airport, _, _ in airports.values()}
    airport_aircrafts = {airport: [] for airport in airport_load}

    for icao, airport_list in aircraft_to_airports.items():
        if len(airport_list) == 1:
            airport_load[airport_list[0][0]] += 1
            airport_aircrafts[airport_list[0][0]].append(icao)
        else:
            closest_airport = min(airport_list, key=lambda x: distance((airports[x[0]][1], airports[x[0]][2]), (x[1], x[2])))
            airport_load[closest_airport[0]] += 1
            airport_aircrafts[closest_airport[0]].append(icao)

    return airport_load, airport_aircrafts

def calculate_relative_threshold(airport_load, relative_threshold=1.3):
    load_values = [load for load in airport_load.values() if load > 2] # Don't bother with low-populated airspaces
    if not load_values:  # Division by 0 error handling
        return 0
    average_load = np.mean(load_values)
    threshold = average_load * relative_threshold
    return threshold

def identify_airport_loads(airport_load, threshold):
    overloaded_airports = {airport: load for airport, load in airport_load.items() if load > threshold}
    underutilized_airports = {airport: load for airport, load in airport_load.items() if load <= threshold and load > 0}
    return overloaded_airports, underutilized_airports

def distance(coord1, coord2):
    return np.sqrt((coord1[0] - coord2[0]) ** 2 + (coord1[1] - coord2[1]) ** 2)

def find_nearest_neighbors(airports, target_airport, k=3):
    distances = [(airport, distance((target_airport[1], target_airport[2]), (coord[1], coord[2]))) for airport, coord in airports.items()]
    distances.sort(key=lambda x: x[1])
    return [airport for airport, _ in distances[1:k+1]]

def adjust_airport_positions(airports, airport_load, threshold):
    overloaded_airports, underutilized_airports = identify_airport_loads(airport_load, threshold)
    new_positions = airports.copy()

    for overloaded in overloaded_airports:
        neighbors = find_nearest_neighbors(new_positions, new_positions[overloaded], k=3)
        # Trier les voisins par charge (du moins chargé au plus chargé)
        neighbors.sort(key=lambda neighbor: airport_load[neighbor])
        for neighbor in neighbors:
            if neighbor in underutilized_airports:
                # Calculer le vecteur de direction
                dir_x = new_positions[overloaded][1] - new_positions[neighbor][1]
                dir_y = new_positions[overloaded][2] - new_positions[neighbor][2]
                length = np.sqrt(dir_x ** 2 + dir_y ** 2)

                if length > 0:
                    dir_x /= length
                    dir_y /= length

                step = ADJUSTMENT_STEP * (airport_load[overloaded] - airport_load[neighbor]) / airport_load[overloaded]

                # Déplacer le voisin sous-utilisé vers l'aéroport surchargé
                new_positions[neighbor] = (
                    new_positions[neighbor][0],
                    new_positions[neighbor][1] + dir_x * step,
                    new_positions[neighbor][2] + dir_y * step
                )

                # Déplacer l'aéroport surchargé dans la direction opposée
                new_positions[overloaded] = (
                    new_positions[overloaded][0],
                    new_positions[overloaded][1] - dir_x * step,
                    new_positions[overloaded][2] - dir_y * step
                )

    return new_positions

def main():
    airports = read_airports_from_csv('data/airports.csv')
    adjusted_airports = airports.copy()
    
    make_process = None

    # Calculer le seuil relatif initial avant la boucle principale
    initial_aircraft_data = {}
    for name, lat, lon in adjusted_airports.values():
        try:
            data = fetch_aircraft_data(lat, lon)
            initial_aircraft_data[name] = data.get('ac', [])  # Vérifier la bonne clé dans la réponse JSON
        except requests.exceptions.RequestException as e:
            print(f"Error fetching data for {name}: {e}")
            continue

    save_aircraft_data_to_csv(initial_aircraft_data, 'data/initial_aircraft_data.csv')
    initial_aircraft_data = read_aircraft_data_from_csv('data/initial_aircraft_data.csv')
    initial_airport_load, _ = calculate_airport_load(adjusted_airports, initial_aircraft_data)
    relative_threshold = calculate_relative_threshold(initial_airport_load, RELATIVE_THRESHOLD)

    print(f"Initial Relative Threshold: {relative_threshold}")

    for iteration in range(MAX_ITERATIONS):
        aircraft_data = {}
        for name, lat, lon in adjusted_airports.values():
            try:
                data = fetch_aircraft_data(lat, lon)
                aircraft_data[name] = data.get('ac', [])  # Vérifier la bonne clé dans la réponse JSON
            except requests.exceptions.RequestException as e:
                print(f"Error fetching data for {name}: {e}")
                continue

        # S'assurer que le dossier de destination existe
        if not os.path.exists('data'):
            os.makedirs('data')

        save_aircraft_data_to_csv(aircraft_data, 'data/aircraft_data.csv')
        aircraft_data = read_aircraft_data_from_csv('data/aircraft_data.csv')
        airport_load, airport_aircrafts = calculate_airport_load(adjusted_airports, aircraft_data)

        print(f"Iteration {iteration + 1}:")
        print("Airport Load:")
        for airport, load in airport_load.items():
            lat, lon = adjusted_airports[airport][1], adjusted_airports[airport][2]
            print(f"{airport}: {load} aircrafts, Coordinates: ({lat}, {lon})")
            print("Aircrafts ICAO:")
            for icao in airport_aircrafts[airport]:
                print(f"  - {icao}")

        # Sauvegarder les charges dans un fichier CSV si nécessaire
        with open('data/airport_load.csv', 'w', newline='') as file:
            writer = csv.writer(file)
            writer.writerow(['airport', 'load'])
            for airport, load in airport_load.items():
                writer.writerow([airport, load])

        if iteration == 0:
            # Mettre à jour airports.csv avec la colonne overloaded et load après la première itération
            with open('data/airports.csv', 'w', newline='') as file:
                writer = csv.writer(file)
                writer.writerow(['name', 'latitude', 'longitude', 'overloaded', 'load'])
                for airport, (name, lat, lon) in airports.items():
                    overloaded = airport_load[airport] > relative_threshold
                    writer.writerow([name, lat, lon, int(overloaded), airport_load[airport]])

        # Ajuster les positions des aéroports
        adjusted_airports = adjust_airport_positions(adjusted_airports, airport_load, relative_threshold)

        # Exécuter "make run" en arrière-plan après la première itération
        if iteration == 0 and make_process is None:
            try:
                make_process = subprocess.Popen(["make", "run"])
            except subprocess.CalledProcessError as e:
                print(f"Error during make run: {e}")

    # Sauvegarder les nouvelles positions ajustées
    with open('data/adjusted_airports.csv', 'w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['name', 'latitude', 'longitude', 'overloaded', 'load'])
        for airport, (name, lat, lon) in adjusted_airports.items():
            overloaded = airport_load[airport] > relative_threshold
            writer.writerow([name, lat, lon, int(overloaded), airport_load[airport]])
    
    # Exécuter "make runopt" à la fin
    try:
        subprocess.Popen(["make", "runopt"])
    except subprocess.CalledProcessError as e:
        print(f"Error during make runopt: {e}")

if __name__ == "__main__":
    main()

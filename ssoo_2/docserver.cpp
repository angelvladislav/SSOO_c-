#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstdlib>
#include <stdexcept>
#include <errno.h>

bool verbose = false;
int port = 8080;  // Puerto por defecto
std::string file_path;

// Función para enviar la respuesta al cliente
void send_response(int client_sock, const std::string& header, const std::string& body = "") {
    std::string response = header + "\r\n\r\n" + body;
    if (verbose) {
        std::cout << "Enviando respuesta: " << response.substr(0, 100) << "..." << std::endl;
    }
    int bytes_sent = send(client_sock, response.c_str(), response.size(), 0);
    if (verbose) {
        std::cout << "Enviados " << bytes_sent << " bytes a través del socket." << std::endl;
    }
}

// Función para procesar los argumentos de la línea de comandos
void parse_args(int argc, char* argv[]) {
    bool file_specified = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Uso: ./docserver [-v | --verbose] [-p <puerto>] <archivo>" << std::endl;
            std::cout << "  -v, --verbose  Muestra información detallada de las operaciones." << std::endl;
            std::cout << "  -h, --help     Muestra este mensaje de ayuda." << std::endl;
            std::cout << "  -p, --port     Especifica el puerto en el que escuchar (por defecto 8080)." << std::endl;
            std::cout << "  <archivo>      El archivo que se servirá a través del servidor." << std::endl;
            exit(0);
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: Se esperaba un número de puerto después de -p o --port." << std::endl;
                exit(1);
            }
        } else {
            file_path = arg;
            file_specified = true;
        }
    }
    if (!file_specified) {
        std::cerr << "Error: No se ha especificado un archivo para servir." << std::endl;
        exit(1);
    }
}

// Función para crear el socket y asignarle el puerto indicado
int make_socket() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Error al crear el socket");
        exit(1);
    }
    if (verbose) {
        std::cout << "Socket creado correctamente." << std::endl;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Error al hacer bind");
        close(sockfd);
        exit(1);
    }
    if (verbose) {
        std::cout << "Socket enlazado al puerto " << port << "." << std::endl;
    }

    return sockfd;
}

// Función para poner el socket a la escucha
void listen_connection(int sockfd) {
    if (listen(sockfd, 5) == -1) {
        perror("Error al poner el socket a la escucha");
        close(sockfd);
        exit(1);
    }
    if (verbose) {
        std::cout << "Servidor escuchando en el puerto " << port << "..." << std::endl;
    }
}

// Función para aceptar una conexión
int accept_connection(int sockfd) {
    int client_sock = accept(sockfd, NULL, NULL);
    if (client_sock == -1) {
        perror("Error al aceptar la conexión");
        close(sockfd);
        exit(1);
    }
    if (verbose) {
        std::cout << "Conexión aceptada." << std::endl;
    }
    return client_sock;
}

// Función para leer el archivo en memoria
std::string read_all(const std::string& path) {
    int file_fd = open(path.c_str(), O_RDONLY);
    if (file_fd == -1) {
        if (errno == EACCES) {
            throw std::runtime_error("Error: No se tienen permisos para leer el archivo.");
        } else if (errno == ENOENT) {
            throw std::runtime_error("Error: El archivo no fue encontrado.");
        } else {
            throw std::runtime_error("Error al abrir el archivo.");
        }
    }

    struct stat file_stat;
    if (fstat(file_fd, &file_stat) == -1) {
        close(file_fd);
        throw std::runtime_error("Error al obtener las estadísticas del archivo.");
    }

    void* mapped_memory = mmap(NULL, file_stat.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
    if (mapped_memory == MAP_FAILED) {
        close(file_fd);
        throw std::runtime_error("Error al mapear el archivo en memoria.");
    }

    std::string body(static_cast<char*>(mapped_memory), file_stat.st_size);

    munmap(mapped_memory, file_stat.st_size);
    close(file_fd);
    
    return body;
}

int main(int argc, char* argv[]) {
    parse_args(argc, argv);

    int sockfd = make_socket();
    listen_connection(sockfd);

    // Bucle principal para aceptar conexiones
    while (true) {
        int client_sock = accept_connection(sockfd);
        
        try {
            std::string body = read_all(file_path);
            std::ostringstream header;
            header << "HTTP/1.1 200 OK\r\nContent-Length: " << body.size();

            send_response(client_sock, header.str(), body);
        } catch (const std::runtime_error& e) {
            send_response(client_sock, "HTTP/1.1 500 Internal Server Error", e.what());
        }

        close(client_sock);  // Cerrar la conexión
    }

    close(sockfd);  // Cerrar el socket del servidor
    return 0;
}

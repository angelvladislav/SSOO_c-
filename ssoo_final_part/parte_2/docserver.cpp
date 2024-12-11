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
#include <expected>

bool verbose = false;
int port = 8080;
std::string file_path;
bool check_file_size = false;

// SafeFD wrapper for safe file descriptor handling
struct SafeFD {
    int fd = -1;

    SafeFD() = default;
    SafeFD(int fd) : fd(fd) {}

    ~SafeFD() {
        if (fd != -1) {
            close(fd);
        }
    }

    operator int() const { return fd; }

    bool is_valid() const { return fd != -1; }

    SafeFD(const SafeFD&) = delete;
    SafeFD& operator=(const SafeFD&) = delete;
};

void send_response(int client_sock, std::string_view header, std::string_view body = {}) {
    std::string response = std::string(header) + "\r\n\r\n" + std::string(body);
    if (verbose) {
        std::cout << "Enviando respuesta: " << response.substr(0, 100) << "..." << std::endl;
    }
    send(client_sock, response.c_str(), response.size(), 0);
}

std::expected<void, int> parse_args(int argc, char* argv[]) {
    bool file_specified = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Uso: ./docserver [-v | --verbose] [-p <puerto>] <archivo>" << std::endl;
            std::cout << "  -v, --verbose  Muestra información detallada de las operaciones." << std::endl;
            std::cout << "  -h, --help     Muestra este mensaje de ayuda." << std::endl;
            std::cout << "  -p, --port     Especifica el puerto en el que escuchar (por defecto 8080)." << std::endl;
            std::cout << "  <archivo>      El archivo que se servirá a través del servidor." << std::endl;
            return {};
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else {
                return std::unexpected(EINVAL);
            }
        } else {
            file_path = arg;
            file_specified = true;
        }
    }
    if (!file_specified) {
        return std::unexpected(EINVAL);
    }
    return {};
}

std::expected<int, int> make_socket(uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        return std::unexpected(errno);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        close(sockfd);
        return std::unexpected(errno);
    }

    return sockfd;
}

std::expected<int, int> accept_connection(const int& socket, sockaddr_in& client_addr) {
    socklen_t addr_len = sizeof(client_addr);
    int client_sock = accept(socket, (struct sockaddr*)&client_addr, &addr_len);
    if (client_sock == -1) {
        return std::unexpected(errno);
    }
    return client_sock;
}

std::expected<void, int> listen_connection(const int& socket) {
    if (listen(socket, 5) == -1) {
        return std::unexpected(errno);
    }
    return {};
}

std::expected<std::string, int> read_all(const std::string& path) {
    SafeFD file_fd(open(path.c_str(), O_RDONLY));
    if (!file_fd.is_valid()) {
        return std::unexpected(errno);
    }

    if (verbose) {
        std::cout << "open: se abre el archivo \"" << path << "\"" << std::endl;
    }

    struct stat file_stat;
    if (fstat(file_fd, &file_stat) == -1) {
        return std::unexpected(errno);
    }

    void* mapped_memory = mmap(NULL, file_stat.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
    if (mapped_memory == MAP_FAILED) {
        return std::unexpected(errno);
    }

    std::string body(static_cast<char*>(mapped_memory), file_stat.st_size);

    if (verbose) {
        std::cout << "read: se leen " << file_stat.st_size << " bytes del archivo \"" << path << "\"" << std::endl;
    }

    munmap(mapped_memory, file_stat.st_size);

    return body;
}

int main(int argc, char* argv[]) {
    auto args_result = parse_args(argc, argv);
    if (!args_result) {
        std::cerr << "Error al analizar argumentos: " << strerror(args_result.error()) << std::endl;
        return args_result.error();
    }

    auto sockfd = make_socket(port);
    if (!sockfd) {
        std::cerr << "Error al crear el socket: " << strerror(sockfd.error()) << std::endl;
        return sockfd.error();
    }

    auto listen_result = listen_connection(sockfd.value());
    if (!listen_result) {
        std::cerr << "Error al poner el socket a la escucha: " << strerror(listen_result.error()) << std::endl;
        close(sockfd.value());
        return listen_result.error();
    }

    std::cout << "Escuchando en el puerto " << port << "..." << std::endl;

    while (true) {
        sockaddr_in client_addr;
        auto client_sock = accept_connection(sockfd.value(), client_addr);
        if (!client_sock) {
            std::cerr << "Error al aceptar la conexión: " << strerror(client_sock.error()) << std::endl;
            close(sockfd.value());
            return client_sock.error();
        }

        try {
            auto body = read_all(file_path);
            if (!body) {
                send_response(client_sock.value(), "HTTP/1.1 500 Internal Server Error", "Error al leer el archivo.");
            } else {
                std::ostringstream header;
                header << "HTTP/1.1 200 OK\r\nContent-Length: " << body.value().size();
                send_response(client_sock.value(), header.str(), body.value());

                if (verbose) {
                    std::cout << "Contenido del archivo:\n" << body.value() << std::endl;
                    std::cout << "Bytes enviados: " << body.value().size() << std::endl;
                }
            }
        } catch (const std::runtime_error& e) {
            send_response(client_sock.value(), "HTTP/1.1 500 Internal Server Error", e.what());
        }

        close(client_sock.value());
    }

    close(sockfd.value());
    return 0;
}

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
#include <array>
#include <sys/types.h>
#include <sys/wait.h>
#include <expected>
#include <arpa/inet.h>

class SafeFD {
public:
    SafeFD(int fd = -1) : fd_(fd) {}
    ~SafeFD() {
        if (fd_ != -1) {
            close(fd_);
        }
    }

    bool is_valid() const { return fd_ != -1; }
    int value() const { return fd_; }

private:
    int fd_;
};

bool verbose = false;
int port = 8080;
std::string base_path;  // Ruta base para los archivos
bool check_file_size = false;

const size_t tam_buffer = 256;

int send_response(const SafeFD& socket, std::string_view header, std::string_view body = {}) {
    // Construir la respuesta completa
    std::string response = std::string(header) + "\r\n\r\n" + std::string(body);

    // Enviar la respuesta a través del socket
    ssize_t bytes_sent = send(socket.value(), response.c_str(), response.size(), 0);
    if (bytes_sent == -1) {
        std::cerr << "Error al enviar la respuesta: " << strerror(errno) << std::endl;
        return errno;
    }

    // Si verbose está activado, mostramos información de la respuesta enviada
    if (verbose) {
        std::cout << "Enviando respuesta: " << response.substr(0, 100) << "..." << std::endl;
    }

    return 0; // Indica éxito
}

std::string read_file(const std::string& path) {
    SafeFD file_fd(open(path.c_str(), O_RDONLY));
    if (!file_fd.is_valid()) {
        return {};
    }

    struct stat file_stat;
    if (fstat(file_fd.value(), &file_stat) == -1) {
        return {};
    }

    void* mapped_memory = mmap(NULL, file_stat.st_size, PROT_READ, MAP_PRIVATE, file_fd.value(), 0);
    if (mapped_memory == MAP_FAILED) {
        return {};
    }

    std::string body(static_cast<char*>(mapped_memory), file_stat.st_size);
    munmap(mapped_memory, file_stat.st_size);

    return body;
}

// Función para configurar las variables de entorno
void set_environment_vars(const std::string& request_path, const std::string& base_dir, uint16_t remote_port, const std::string& remote_ip) {
    setenv("REQUEST_PATH", request_path.c_str(), 1);  // Configurar REQUEST_PATH
    setenv("SERVER_BASEDIR", base_dir.c_str(), 1);    // Configurar SERVER_BASEDIR
    setenv("REMOTE_PORT", std::to_string(remote_port).c_str(), 1);  // Configurar REMOTE_PORT
    setenv("REMOTE_IP", remote_ip.c_str(), 1);        // Configurar REMOTE_IP
}

std::expected<void, int> parse_args(int argc, char* argv[]) {
    bool file_specified = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Uso: ./docserver [-v | --verbose] [-p <puerto>] [-b <ruta> | --base <ruta>] \n";
            std::cout << "  -v, --verbose  Muestra información detallada de las operaciones." << std::endl;
            std::cout << "  -h, --help     Muestra este mensaje de ayuda." << std::endl;
            std::cout << "  -p, --port     Especifica el puerto en el que escuchar (por defecto 8080)." << std::endl;
            std::cout << "  -b, --base     Directorio base donde buscar los archivos." << std::endl;
            return {};
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else {
                return std::unexpected(EINVAL);
            }
        } else if (arg == "-b" || arg == "--base") {
            if (i + 1 < argc) {
                base_path = argv[++i];
            } else {
                return std::unexpected(EINVAL);
            }
        }
    }

    // Si no se especifica base_path, usa el valor de la variable de entorno o el directorio actual
    if (base_path.empty()) {
        const char* env_base = std::getenv("DOCSERVER_BASEDIR");
        if (env_base) {
            base_path = env_base;
        } else {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd))) {
                base_path = cwd;
            } else {
                return std::unexpected(EINVAL);
            }
        }
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

std::expected<std::string, int> receive_request(const SafeFD& socket, size_t max_size) {
    std::string request(max_size, 0);
    ssize_t bytes_received = recv(socket.value(), &request[0], max_size, 0);
    if (bytes_received == -1) {
        return std::unexpected(errno);
    }
    request.resize(bytes_received); // Ajustamos al tamaño real de la petición recibida
    return request;
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

        pid_t pid = fork();
        if (pid == 0) { // Proceso hijo
            close(sockfd.value());  // El hijo no necesita el socket del servidor

            std::string request;
            auto request_result = receive_request(client_sock.value(), 1024);  // Maximo de 1024 bytes
            if (!request_result) {
                std::cerr << "Error al recibir la solicitud: " << strerror(request_result.error()) << std::endl;
                send_response(client_sock.value(), "HTTP/1.1 400 Bad Request", "Error al recibir la solicitud.");
                close(client_sock.value());
                return EXIT_FAILURE;
            }

            request = request_result.value();
            std::istringstream iss(request);
            std::string method, file_path;
            iss >> method >> file_path;

            if (method != "GET" || file_path.empty() || file_path[0] != '/') {
                send_response(client_sock.value(), "HTTP/1.1 400 Bad Request", "Solicitud no válida.");
                close(client_sock.value());
                return EXIT_FAILURE;
            }

            // Configurar las variables de entorno
            set_environment_vars(file_path, base_path, ntohs(client_addr.sin_port), inet_ntoa(client_addr.sin_addr));

            file_path = base_path + file_path;
            auto body = read_file(file_path);
            if (body.empty()) {
                send_response(client_sock.value(), "HTTP/1.1 404 Not Found", "Archivo no encontrado.");
            } else {
                send_response(client_sock.value(), "HTTP/1.1 200 OK", body);
            }

            close(client_sock.value());  // Cerrar conexión con el cliente
            return EXIT_SUCCESS;
        } else {  // Proceso padre
            close(client_sock.value());  // El padre no necesita el socket del cliente
            waitpid(pid, NULL, 0);  // Esperar a que el proceso hijo termine
        }
    }

    close(sockfd.value());
    return EXIT_SUCCESS;
}

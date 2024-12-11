#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <sys/stat.h>
#include <stdexcept>
#include <cerrno>
#include <cstring>  // Para strerror
#include <expected> // Para std::expected

bool verbose = false;
std::string file_path;

void show_response(std::string_view header, std::string_view body = {}) {
    std::string response = std::string(header) + "\r\n\r\n" + std::string(body);
    if (verbose) {
        std::cout << "Mostrando respuesta: " << response.substr(0, 100) << "..." << std::endl;
    }
    std::cout << response << std::endl;
}

// Función para leer el archivo completo y manejar los errores correctamente.
std::expected<std::string, int> read_all(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(errno);  // Devuelve el código de error si no se puede abrir el archivo.
    }

    std::ostringstream content;
    content << file.rdbuf();
    return content.str();  // Devuelve el contenido del archivo.
}

// Función para analizar los argumentos y manejar errores.
std::expected<void, int> parse_args(int argc, char* argv[]) {
    bool file_specified = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Uso: ./docserver [-v | --verbose] [-h | --help] <archivo>" << std::endl;
            std::cout << "  -v, --verbose  Muestra información detallada de las operaciones." << std::endl;
            std::cout << "  -h, --help     Muestra este mensaje de ayuda." << std::endl;
            std::cout << "  <archivo>      El archivo que se servirá." << std::endl;
            return {};  // Retorna vacío en caso de que se haya solicitado ayuda.
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else {
            file_path = arg;
            file_specified = true;
        }
    }

    if (!file_specified) {
        return std::unexpected(EINVAL);  // Retorna error si no se especifica el archivo.
    }

    return {};  // Retorna vacío si los argumentos son válidos.
}

int main(int argc, char* argv[]) {
    auto args_result = parse_args(argc, argv);
    if (!args_result) {
        std::cerr << "Error al analizar argumentos: " << strerror(args_result.error()) << std::endl;
        return args_result.error();  // Regresa el código de error del parseo de los argumentos.
    }

    auto body_result = read_all(file_path);
    if (!body_result) {
        std::cerr << "Error al leer el archivo: " << strerror(body_result.error()) << std::endl;
        return body_result.error();  // Regresa el código de error al leer el archivo.
    }

    // Mostrar el Content-Length y el contenido del archivo.
    std::ostringstream header;
    header << "Content-Length: " << body_result.value().size();
    show_response(header.str(), body_result.value());

    if (verbose) {
        std::cout << "Contenido del archivo:\n" << body_result.value() << std::endl;
        std::cout << "Bytes leídos: " << body_result.value().size() << std::endl;
    }

    return 0;  // Si todo va bien, se regresa 0 indicando éxito.
}

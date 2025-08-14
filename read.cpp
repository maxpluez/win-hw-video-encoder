#include <iostream>
#include <fstream>
#include <vector>

std::vector<unsigned char> readFileAsBytes(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file");
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> buffer(size);
    if (size > 0) {
       if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            throw std::runtime_error("Error while reading the file");
       }
    }

    file.close();
    return buffer;
}

int main() {
    try {
        std::vector<unsigned char> bytes = readFileAsBytes("C:\\Users\\maxzhang\\Desktop\\firstfixedsw\\val0.h264");
        std::cout << "File read successfully. Size: " << bytes.size() << " bytes." << std::endl;
        for (int i = 0; i < 300; ++i) {
            printf("%u, ", bytes[i]);
        }
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
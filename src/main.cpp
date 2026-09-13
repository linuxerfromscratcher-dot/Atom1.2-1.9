#include <iostream>
#include <vector>
#include <cstdint>
#include <fstream>

struct FileContents {
    std::string contents;
    int status;
};

FileContents read_file(std::string path) {
    std::string line;
    std::string lines;

    std::ifstream file(path);

    if (file.is_open()) {
        while (std::getline(file, line)) {
            lines += line + "\n";
        }
        file.close();
    } else {
        return FileContents {"", 1};
    }

    return FileContents {lines, 0};
}

std::string join(const std::vector<std::string>& strings, const std::string& separator) {
    std::string result;

    for (size_t i = 0; i < strings.size(); ++i) {
        if (i > 0)
            result += separator;

        result += strings[i];
    }

    return result;
}

enum TokenType {
    OpCode,
    Value
};

struct Token {
    TokenType type;
    int64_t value;
};

std::string clean_code(const std::string& code) {
    std::vector<std::string> split;
    split.emplace_back();

    bool skip = false;

    for (char c : code) {
        if (c == '\n') {
            skip = false;
            split.emplace_back();
            continue;
        }

        if (c == ';') {
            skip = true;
            continue;
        }

        if (skip)
            continue;

        if (c == ' ') {
            if (!split.back().empty() && split.back().back() != ' ')
                split.back() += c;
        } else {
            split.back() += c;
        }
    }

    return join(split, "");
}

std::vector<std::string> split_tokens(std::string split_code) {
    std::vector<std::string> tokens = {0};
}

std::vector<Token> tokenize(std::vector<std::string> split_code) {

}

int main(int argc, char** argv) {
    if (argc <= 1) {
        std::cerr << "\x1b[31;1mE:\x1b[0m Expected file name. Got None" << std::endl;
        return 1;
    }

    FileContents f = read_file(argv[1]);
    if (f.status == 1) {
        std::cerr << "\x1b[31;1mE:\x1b[0m Failed to read file." << std::endl;
        return 1;
    }

    std::string code = f.contents;

    std::cout << clean_code(code) << std::endl;

    return 0;
}

#pragma once
#include <chrono>
#include <string>
#include <vector>

// printf-style formatting into a std::string.
std::string format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// A table that is printed to the terminal row by row (so long runs show progress)
// and can afterwards be saved as CSV. Every cell is a plain string, so what is
// printed is exactly what ends up in the CSV.
class ReportTable{
    struct Column{
        std::string header;
        int width;
    };
    std::vector<Column> columns;
    std::vector<std::vector<std::string>> rows;
    std::chrono::steady_clock::time_point start;    // when the table (i.e. the part) started

    void print_line(const std::vector<std::string>& cells) const;

public:
    ReportTable(const std::string& title, const std::vector<std::pair<std::string, int>>& headers_and_widths);

    // Prints the row immediately and keeps it for write_csv().
    void add_row(const std::vector<std::string>& cells);

    // Prints the time elapsed since the table was created, then writes the header and
    // all rows to path (creating its folder if needed).
    void write_csv(const std::string& path) const;
};

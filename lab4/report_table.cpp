#include "report_table.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

std::string format(const char* fmt, ...){
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

ReportTable::ReportTable(const std::string& title, const std::vector<std::pair<std::string, int>>& headers_and_widths)
    : start(std::chrono::steady_clock::now()){
    int total_width = 0;
    for(const auto& column : headers_and_widths){
        columns.push_back({column.first, column.second});
        total_width += column.second + 2;
    }
    printf("\n%s\n", title.c_str());
    std::vector<std::string> headers;
    for(const Column& column : columns) headers.push_back(column.header);
    print_line(headers);
    printf("%s\n", std::string(total_width, '-').c_str());
}

// The first column is left-aligned, all others are right-aligned.
void ReportTable::print_line(const std::vector<std::string>& cells) const{
    for(size_t i = 0; i < columns.size(); ++i){
        const char* text = i < cells.size() ? cells[i].c_str() : "";
        if(i == 0) printf("%-*s  ", columns[i].width, text);
        else printf("%*s  ", columns[i].width, text);
    }
    printf("\n");
    fflush(stdout);
}

void ReportTable::add_row(const std::vector<std::string>& cells){
    print_line(cells);
    rows.push_back(cells);
}

void ReportTable::write_csv(const std::string& path) const{
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    printf("\nTotal time for this part: %.2f s\n", elapsed);
    std::filesystem::path folder = std::filesystem::path(path).parent_path();
    if(!folder.empty()) std::filesystem::create_directories(folder);
    FILE* out = fopen(path.c_str(), "w");
    if(!out){
        fprintf(stderr, "Could not write %s\n", path.c_str());
        exit(1);
    }
    for(size_t i = 0; i < columns.size(); ++i){
        fprintf(out, "%s%s", i ? "," : "", columns[i].header.c_str());
    }
    fprintf(out, "\n");
    for(const auto& row : rows){
        for(size_t i = 0; i < row.size(); ++i){
            fprintf(out, "%s%s", i ? "," : "", row[i].c_str());
        }
        fprintf(out, "\n");
    }
    fclose(out);
    printf("\nSaved %s\n", path.c_str());
}

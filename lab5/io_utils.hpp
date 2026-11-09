#pragma once
#include <cstddef>

bool readn(int fd, void* buf, size_t n);
bool writen(int fd, const void* buf, size_t n);
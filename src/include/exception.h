//
// Created by dreamHuang on 2023/2/25.
//

#ifndef HUCORO_EXCEPTION_H
#define HUCORO_EXCEPTION_H

#include <stdexcept>
#include <string>

namespace hucoro {
struct HuCoroGeneralErr : std::logic_error {
    HuCoroGeneralErr(const char *message) : std::logic_error(message) {}
};
}// namespace hucoro

#endif//HUCORO_EXCEPTION_H

#pragma once

#include <string>
#include <stdexcept>

namespace mirai
{
    /**
     * \brief Base URL of all the HTTP requests
     */
    inline std::string base_url = "localhost:8080"; // TODO: maybe move this into Session class

    /**
     * \brief Exception class for runtime errors in mirai API
     */
    class RuntimeError : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /**
     * \brief A simple error handler which logs every error to the console
     */
    void error_logger();

    /**
     * \brief A simple error handler which rethrows every error
     */
    inline void error_rethrower() { throw; }
}

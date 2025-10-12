/* 
 * This file is part of the SDRPP distribution (https://github.com/qrp73/SDRPP).
 * Copyright (c) 2025 qrp73.
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once
#include <string>


namespace net::xhr {

    std::string url_decode(const std::string& s);
    
    struct UrlParts {
        std::string scheme;
        std::string hostPort;
        std::string host;
        int port = -1;
        std::string path;
    };
    
    UrlParts parseUrl(const std::string& url);

    std::string request_xhr(const std::string& url);

}

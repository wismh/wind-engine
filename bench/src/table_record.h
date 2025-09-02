#pragma once

#include <string>

namespace bench {

// One row of the table scene, already formatted for its five columns.
struct TableRecord {
    int id = 0;
    std::string id_text;
    std::string name;
    std::string value;
    std::string status;
    std::string ratio;
};

}

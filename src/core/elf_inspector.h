#pragma once

#include <QString>

namespace srcds64 {

class ElfInspector {
public:
    static bool isElf64(const QString &path);
};

}

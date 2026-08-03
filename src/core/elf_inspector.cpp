#include "core/elf_inspector.h"

#include <QFile>

namespace srcds64 {

bool ElfInspector::isElf64(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray header = file.read(5);
    return header.size() == 5
        && static_cast<unsigned char>(header[0]) == 0x7f
        && header[1] == 'E'
        && header[2] == 'L'
        && header[3] == 'F'
        && static_cast<unsigned char>(header[4]) == 2;
}

}

#include "services/identities.h"

#include <QDir>

#include "services/app_info.h"
#include "services/state_db.h"

namespace ot {

Identities& Identities::shared() {
    static Identities identities(StateDb::shared(), QDir(app::dataDir()).filePath(QStringLiteral("ids")));
    return identities;
}

Identities::Identities(StateDb& db, const QString& idsDir)
    : db_(db), store_(db.handle(), std::filesystem::path(idsDir.toStdWString()), &protector_) {}

std::mutex& Identities::mutex() { return db_.mutex(); }

}  // namespace ot

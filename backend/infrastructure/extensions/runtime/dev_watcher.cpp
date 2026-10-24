#include "infrastructure/extensions/runtime/dev_watcher.h"
#include <QDirIterator>
#include <QFileSystemWatcher>

namespace nekotune {
DevWatcher::DevWatcher(const QString &directory, QObject *parent) : QObject(parent) {
    // QFileSystemWatcher is not recursive and reports content changes only for watched files.
    QStringList paths{directory};
    QDirIterator items(directory, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (items.hasNext() && paths.size() < 1024) {
        const auto path = items.next();
        if (!path.contains("/node_modules/") && !path.endsWith(".tmp"))
            paths.append(path);
    }
    m_watcher = std::make_unique<QFileSystemWatcher>(paths);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(350);
    connect(&m_debounce, &QTimer::timeout, this, &DevWatcher::changed);
    connect(m_watcher.get(), &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(m_watcher.get(), &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
}
DevWatcher::~DevWatcher() = default;
} // namespace nekotune

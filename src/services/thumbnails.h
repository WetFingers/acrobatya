// Миниатюры первой страницы для Главной (эталон 3.18, 4.7; view-29): ширина 160 px, рендер в своём пуле (два
// потока — не мешают рендеру вкладок), кеш на диске <каталог данных>/thumbs/<sha1>.png (sha1 от пути, времени
// изменения и размера — правленный файл получает новую миниатюру) и в памяти. get() отдаёт готовую или пустую
// и ставит рендер (один на файл); готовность — сигнал ready(path) в потоке объекта. Не открывшийся файл
// (пароль, повреждён) миниатюры не получает и не перерисовывается снова. Главная зовёт get() только из
// отрисовки видимых карточек — миниатюры невидимых не считаются. Поток: GUI (get, сигналы); рендер — в пуле.
#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>

#include <atomic>

namespace ot {

class ThumbnailCache : public QObject {
    Q_OBJECT
public:
    static constexpr int kWidth = 160;

    static ThumbnailCache& shared();  // <каталог данных>/thumbs
    explicit ThumbnailCache(QString dir, int width = kWidth, QObject* parent = nullptr);
    ~ThumbnailCache() override;

    QImage get(const QString& path);
    bool failed(const QString& path) const { return failed_.contains(path); }
    // Сколько рендеров поставлено (готовые с диска и из памяти не считаются).
    int renders() const { return renders_.load(); }
    QString cacheFile(const QString& path) const;
    QString dir() const { return dir_; }
    void waitForDone();
    // Выход: очередь очищается, идущий рендер дожидается, новые get() рендер не ставят.
    void stop();
    void clearMemory();

signals:
    void ready(const QString& path);

private:
    void finished(const QString& path, const QImage& image);

    QString dir_;
    int width_;
    QThreadPool pool_;
    QHash<QString, QImage> memory_;
    QSet<QString> pending_;
    QSet<QString> failed_;
    std::atomic<int> renders_{0};
    bool stopped_ = false;
};

}  // namespace ot

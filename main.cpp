#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QThread>
#include <QFile>
#include <QCryptographicHash>
#include <QDataStream>
#include <QPalette>
#include <QStyleFactory>
#include <QPainter>

enum class PatchStage
{
    CheckingHash,
    CreateBackup,
    ApplyPatch
};

void applyPatch(QString origPath, QString patchPath, std::function<void(PatchStage)> progressCallback)
{
    const QByteArray MAGIC = "PTCH";
    const quint8 VERSION = 1;

    QFile patchFile = QFile(patchPath);
    if (!patchFile.open(QIODevice::ReadOnly))
        throw std::runtime_error(("failed to open file " + patchPath).toStdString());

    QDataStream in = QDataStream(&patchFile);
    in.setByteOrder(QDataStream::LittleEndian);

    QByteArray magic = QByteArray(MAGIC.size(), 0);
    in.readRawData(magic.data(), MAGIC.size());

    if (magic != MAGIC)
        throw std::runtime_error("invalid patch format");

    quint8 version;
    in >> version;

    if (version != VERSION)
        throw std::runtime_error("unsupported patch version");

    QFile origFile = QFile(origPath);
    if (!origFile.open(QIODevice::ReadOnly))
        throw std::runtime_error(("failed to open file " + origPath).toStdString());

    const int HASH_SIZE = 32;
    QByteArray expectedHash = patchFile.read(HASH_SIZE);
    QCryptographicHash hash = QCryptographicHash(QCryptographicHash::Sha256);

    const quint64 BLOCK = 4 * 1024 * 1024;

    progressCallback(PatchStage::CheckingHash);
    while (!origFile.atEnd())
        hash.addData(origFile.read(BLOCK));

    QByteArray realHash = hash.result();

    if (realHash != expectedHash)
        throw std::runtime_error("invalid executable, check game version and build");

    origFile.close();

    QString backupPath = origPath + ".bak"; 
    progressCallback(PatchStage::CreateBackup);
    if (!QFile::exists(backupPath))
        if (!QFile::copy(origPath, backupPath))
            throw std::runtime_error("failed to create backup");

    if (!origFile.open(QIODevice::ReadWrite))
        throw std::runtime_error("failed to open file for writing patch");

    progressCallback(PatchStage::ApplyPatch);
    while (!in.atEnd())
    {
        quint64 offset;
        quint8 value;

        in >> offset;
        in >> value;

        origFile.seek(offset);
        origFile.putChar(value);
    }
    
    origFile.close();
    patchFile.close();
}

enum class fileType
{
    exe,
    bin
};

class DropZone : public QLabel//QFrame//QLabel
{
    Q_OBJECT

public:
    fileType type;
    QString path;
    QString title;
    bool hovered = false;
    bool dragging = false;
    bool hasFile = false;

public:
    DropZone(QString title, fileType type, QWidget *parent = nullptr) 
                : QLabel(parent), title(title), type(type)
    {
        QLabel::setAlignment(Qt::AlignCenter);
        QLabel::setAcceptDrops(true);

        QWidget::setAutoFillBackground(true);
        QWidget::setBackgroundRole(QPalette::Base);

        QLabel::setText(title + "\n\n(Browse or drop a file)");
        this->updateStyle();
    }

protected:
    void mousePressEvent(QMouseEvent *) override
    {
        QString buff;
        if(this->type == fileType::exe)
            buff = "Executable files (*.exe)";
        else
            buff = "Binary files (*.bin)";
        
        QString p = QFileDialog::getOpenFileName(this, "Select a file " + this->title, "", buff + ";;All Files (*)");

        if (!p.isEmpty())
            this->setPath(p);
    }

    void enterEvent(QEnterEvent *) override
    {
        this->hovered = true;
        this->updateStyle();
    }

    void leaveEvent(QEvent *) override
    {
        this->hovered = false;
        this->updateStyle();
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasUrls())
        {
            this->dragging = true;
            this->updateStyle();
            event->acceptProposedAction();
        }
    }

    void dragLeaveEvent(QDragLeaveEvent *) override
    {
        this->dragging = false;
        this->updateStyle();
    }

    void dropEvent(QDropEvent *event) override
    {
        this->dragging = false;
        this->updateStyle();
        this->setPath(event->mimeData()->urls().first().toLocalFile());
    }

private:

    void setPath(QString p)
    {
        this->path = p;
        this->hasFile = true;
        QLabel::setText(title + "\n\n" + p);
        this->updateStyle();
    }
    
    /*
    void updateStyle()
    {
        QString border;
        QString bg;

        if (this->hasFile)
            border = "border:2px dashed #60B3F2";
        else
            border = "border:2px dashed #888888";

        if (this->dragging)
            bg = "background-color: #d6e2e0";
        else if (this->hovered)
            bg = "background-color: #e4f2fa";
        else
            bg = "background-color: #f9f9f9";

        QLabel::setStyleSheet(QString(
                        "QLabel{ %1; padding:30px; font-size:14px; %2; }")
                        .arg(border)
                        .arg(bg));
    }
    */
   
    void updateStyle()
    {
        QPalette p = palette();

        if (dragging)
            setAutoFillBackground(true);
        else if (hovered)
            setAutoFillBackground(true);
        else
            setAutoFillBackground(false);

        QWidget::update();
    }

    void paintEvent(QPaintEvent *event) override
    {

        QPainter painter = QPainter(this);
        QColor bg;        
        QColor border;

        if (this->dragging)
        {
            bg = palette().color(QPalette::Highlight).darker(250);
            painter.fillRect(QWidget::rect(), bg);
        }
        else if (this->hovered)
        {
            bg = palette().color(QPalette::Highlight).darker(250);
            painter.fillRect(QWidget::rect(), bg);
        }

        if (this->hasFile)
            border = palette().color(QPalette::Highlight);
        else
            border = palette().color(QPalette::Mid).lighter(150);

        painter.setPen(QPen(border, 2, Qt::DashLine));
        painter.drawRect(QWidget::rect());
        
        QLabel::paintEvent(event);
    }
    
};

class PatchWorker : public QThread 
{
    Q_OBJECT

private:
    QString exec;
    QString patch;

public:
    PatchWorker(QString e, QString p)
        : exec(e), patch(p) {}

signals:
    void progress(PatchStage);
    void finished();
    void error(QString);

protected:
    void run() override
    {
        try
        {
            applyPatch(exec, patch, [&](PatchStage p){ emit progress(p); });
            emit this->finished();
        }
        catch (std::exception &e)
        {
            emit this->error(e.what());
        }
    }
};

class App : public QWidget
{
    Q_OBJECT

private:
    DropZone *exec;
    DropZone *patch;
    QPushButton *btn;
    QLabel *status;
    PatchWorker *worker = nullptr;
public:
    App()
    {
        setWindowTitle("Patcher");
        resize(600, 300);

        QVBoxLayout *layout = new QVBoxLayout;
        this->exec = new DropZone("Executable", fileType::exe);
        this->patch = new DropZone("Patch", fileType::bin);

        layout->addWidget(exec);
        layout->addWidget(patch);

        this->btn = new QPushButton("Apply patch");
        layout->addWidget(this->btn);

        this->status = new QLabel("");
        layout->addWidget(status);

        QWidget::setLayout(layout);

        QObject::connect(this->btn, &QPushButton::clicked, this, &App::run); 
    }

private slots:
    void run()
    {
        this->btn->setEnabled(false);

        this->status->setText("");

        if (this->exec->path.isEmpty() || this->patch->path.isEmpty())
        {
            this->fail("select both files");
            return;
        }

        this->worker = new PatchWorker(this->exec->path, this->patch->path);

        QObject::connect(this->worker, &PatchWorker::progress, this, &App::progressText);
        QObject::connect(this->worker, &PatchWorker::finished, this, &App::done);
        QObject::connect(this->worker, &PatchWorker::error, this, &App::fail);
        //QObject::connect(this->worker, &QThread::finished, this->worker, &QObject::deleteLater);
        //QObject::connect(this->worker, &QThread::finished, this, [this](){ this->worker = nullptr; });

        this->worker->QThread::start();
    }

    void progressText(PatchStage stage)
    {
        QString message;

        if (stage == PatchStage::CheckingHash)
            message = "🔍 Checking file...";
        else if (stage == PatchStage::CreateBackup)
            message = "💾 Creating backup " + this->exec->path + ".bak";
        else if (stage == PatchStage::ApplyPatch)
            message = "⚙ Applying patch...";
        
        this->status->setText(this->status->text() + "\n" + message);
    }

    void done()
    {
        this->status->setText(this->status->text() + "\n✅ Patch applied");
        this->btn->setEnabled(true);
        if (this->worker)
            this->worker->QObject::deleteLater();
        this->worker = nullptr;
    }

    void fail(QString message)
    {
        this->status->setText(this->status->text() + "\n❌ Error: " + message);
        this->btn->setEnabled(true);
        if (this->worker)
            this->worker->QObject::deleteLater();
        this->worker = nullptr;
    }
};


int main(int argc, char *argv[])
{
    QApplication app = QApplication(argc, argv);

    app.setStyle(QStyleFactory::create("Fusion"));

    App w = App();
    w.QWidget::show();

    return app.exec();
}

#include "main.moc"
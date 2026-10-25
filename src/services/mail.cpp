#include "services/mail.h"

#include <QFile>

#include <spdlog/spdlog.h>

#include <deque>
#include <string>
#include <vector>


#include <windows.h>  // NOMINMAX, WIN32_LEAN_AND_MEAN — у целей (CMakeLists.txt)
#include <mapi.h>

namespace ot {

const char* mailStatusName(MailResult::Status status) noexcept {
    switch (status) {
        case MailResult::Status::Sent:
            return "sent";
        case MailResult::Status::Cancelled:
            return "cancelled";
        case MailResult::Status::NoClient:
            return "no-client";
        case MailResult::Status::Failed:
            break;
    }
    return "failed";
}

namespace mail_impl {

using SendMailW = ULONG(WINAPI*)(LHANDLE, ULONG_PTR, lpMapiMessageW, FLAGS, ULONG);

MailResult result(MailResult::Status status, const QString& message) {
    MailResult r;
    r.status = status;
    r.message = message;
    return r;
}

}  // namespace mail_impl

MailResult MapiMailer::send(const MailMessage& m) {
    using mail_impl::result;
    HMODULE dll = LoadLibraryW(L"MAPI32.DLL");
    if (dll == nullptr) {
        return result(MailResult::Status::NoClient, QStringLiteral("MAPI32.DLL is not available"));
    }
    const auto sendMail = reinterpret_cast<mail_impl::SendMailW>(GetProcAddress(dll, "MAPISendMailW"));
    if (sendMail == nullptr) {
        FreeLibrary(dll);
        return result(MailResult::Status::NoClient, QStringLiteral("MAPISendMailW is not exported"));
    }
    // Строки живут до конца вызова (deque — адреса не двигаются).
    std::deque<std::wstring> strings;
    const auto keep = [&strings](const QString& s) {
        strings.push_back(s.toStdWString());
        return const_cast<PWSTR>(strings.back().c_str());
    };
    std::vector<MapiRecipDescW> recipients;
    const auto add = [&](const QStringList& list, ULONG kind) {
        for (const QString& address : list) {
            MapiRecipDescW r{};
            r.ulRecipClass = kind;
            r.lpszName = keep(address);
            r.lpszAddress = keep(QStringLiteral("SMTP:") + address);
            recipients.push_back(r);
        }
    };
    add(m.to, MAPI_TO);
    add(m.cc, MAPI_CC);
    add(m.bcc, MAPI_BCC);
    std::vector<MapiFileDescW> files;
    for (const MailMessage::Attachment& a : m.attachments) {
        MapiFileDescW f{};
        f.nPosition = static_cast<ULONG>(-1);
        f.lpszPathName = keep(a.path);
        f.lpszFileName = keep(a.name);
        files.push_back(f);
    }
    MapiMessageW msg{};
    msg.lpszSubject = keep(m.subject);
    msg.lpszNoteText = keep(m.body);
    msg.nRecipCount = static_cast<ULONG>(recipients.size());
    msg.lpRecips = recipients.empty() ? nullptr : recipients.data();
    msg.nFileCount = static_cast<ULONG>(files.size());
    msg.lpFiles = files.empty() ? nullptr : files.data();
    const ULONG rc = sendMail(0, static_cast<ULONG_PTR>(hwnd_), &msg, MAPI_DIALOG | MAPI_LOGON_UI, 0);
    FreeLibrary(dll);
    spdlog::info("mail: MAPISendMailW -> {}", rc);
    switch (rc) {
        case SUCCESS_SUCCESS:
            return result(MailResult::Status::Sent, {});
        case MAPI_USER_ABORT:
            return result(MailResult::Status::Cancelled, QStringLiteral("user cancelled"));
        case MAPI_E_LOGIN_FAILURE:
        case MAPI_E_NOT_SUPPORTED:
        case MAPI_E_FAILURE:
            return result(MailResult::Status::NoClient, QStringLiteral("no MAPI mail client (%1)").arg(rc));
        default:
            return result(MailResult::Status::Failed, QStringLiteral("MAPISendMailW error %1").arg(rc));
    }
}

MailResult FakeMailer::send(const MailMessage& message) {
    const std::lock_guard lock(mutex_);
    messages_.push_back(message);
    for (const MailMessage::Attachment& a : message.attachments) {
        QFile f(a.path);
        bytes_ << (f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray());
    }
    MailResult r;
    r.status = status_;
    return r;
}

std::vector<MailMessage> FakeMailer::messages() const {
    const std::lock_guard lock(mutex_);
    return messages_;
}

QList<QByteArray> FakeMailer::attachmentBytes() const {
    const std::lock_guard lock(mutex_);
    return bytes_;
}

namespace mail_impl {

Mailer*& current() {
    static MapiMailer real;
    static Mailer* active = &real;
    return active;
}

}  // namespace mail_impl

Mailer& mailer() { return *mail_impl::current(); }

ScopedMailer::ScopedMailer(Mailer& mailer) : previous_(mail_impl::current()) { mail_impl::current() = &mailer; }

ScopedMailer::~ScopedMailer() { mail_impl::current() = previous_; }

}  // namespace ot

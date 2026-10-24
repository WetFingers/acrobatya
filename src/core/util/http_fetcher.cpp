#include "core/util/http_fetcher.h"

#include <algorithm>
#include <cctype>

namespace ot::net {

const char* statusName(Status status) {
    switch (status) {
        case Status::Ok:
            return "ok";
        case Status::Disabled:
            return "disabled";
        case Status::BadUrl:
            return "bad url";
        case Status::Timeout:
            return "timeout";
        case Status::NetworkError:
            return "network error";
        case Status::HttpError:
            return "http error";
        case Status::RedirectRefused:
            return "redirect refused";
        case Status::TooLarge:
            return "response too large";
    }
    return "unknown";
}

std::string mediaType(const std::string& contentType) {
    std::string out = contentType.substr(0, contentType.find(';'));
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    out.erase(out.begin(), std::find_if(out.begin(), out.end(), notSpace));
    out.erase(std::find_if(out.rbegin(), out.rend(), notSpace).base(), out.end());
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

void FakeHttpFetcher::add(const std::string& url, Response response) {
    const std::lock_guard lock(mutex_);
    responses_[url] = std::move(response);
}

void FakeHttpFetcher::addBody(const std::string& url, std::vector<std::uint8_t> body,
                              std::string contentType) {
    Response r;
    r.status = Status::Ok;
    r.httpCode = 200;
    r.contentType = mediaType(contentType);
    r.body = std::move(body);
    r.finalUrl = url;
    add(url, std::move(r));
}

void FakeHttpFetcher::setHandler(Handler handler) {
    const std::lock_guard lock(mutex_);
    handler_ = std::move(handler);
}

Response FakeHttpFetcher::fetch(const Request& request) {
    Handler handler;
    {
        const std::lock_guard lock(mutex_);
        requests_.push_back(request);
        const auto it = responses_.find(request.url);
        if (it != responses_.end()) {
            return it->second;
        }
        handler = handler_;
    }
    if (handler) {
        return handler(request);
    }
    Response r;
    r.status = Status::NetworkError;
    r.error = "no fake response for " + request.url;
    return r;
}

std::vector<Request> FakeHttpFetcher::requests() const {
    const std::lock_guard lock(mutex_);
    return requests_;
}

std::size_t FakeHttpFetcher::requestCount() const {
    const std::lock_guard lock(mutex_);
    return requests_.size();
}

void FakeHttpFetcher::clearRequests() {
    const std::lock_guard lock(mutex_);
    requests_.clear();
}

}  // namespace ot::net

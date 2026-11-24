#pragma once

#include <QNetworkReply>

namespace Net {

// A failure worth another attempt at the same URL — the link, not the
// answer, was at fault.
bool isTransientError(QNetworkReply::NetworkError error);

} // namespace Net

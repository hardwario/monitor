// Plan 0037 L3 (ADR-0258, ADR-0259): the documentation anchors are a contract. For every topic of
// the docs table and every docs language the page exists (English-only pages in `en`) and has the
// anchor, as an `<a id="x">` line or a heading whose GitHub slug is x; every errorTable id has an
// `<a id="<id>">` line in each language's troubleshooting.md. Reads HWIO_DOCS_DIR (the source
// tree's docs/), no network.

#include "hwio/core/docs_urls.h"
#include "hwio/core/error.h"

#include <QFile>
#include <QIODevice>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QStringView>
#include <QtTypes>

#include <gtest/gtest.h>

#include <cstddef>
#include <string_view>

#ifndef HWIO_DOCS_DIR
#error "HWIO_DOCS_DIR must be defined (libs/core/tests/CMakeLists.txt)"
#endif

namespace {

QString q(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString pagePath(std::string_view language, std::string_view page) {
    return QStringLiteral(HWIO_DOCS_DIR "/") + q(language) + u'/' + q(page) + QStringLiteral(".md");
}

// The page text, or an empty string when the file is missing or unreadable.
QString readPage(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool hasExplicitAnchor(const QString& text, QStringView id) {
    return text.contains(QStringLiteral("<a id=\"") + id + QStringLiteral("\"></a>"));
}

// GitHub's heading slug: lower case, punctuation except '-' and '_' removed, spaces to '-'.
QString githubSlug(QStringView heading) {
    QString slug;
    for (const QChar c : heading.trimmed().toString().toLower()) {
        if (c.isLetterOrNumber() || c == u'-' || c == u'_') {
            slug.append(c);
        } else if (c == u' ') {
            slug.append(u'-');
        }
    }
    return slug;
}

bool hasHeadingAnchor(const QString& text, QStringView id) {
    static const QRegularExpression heading(QStringLiteral(R"(^#{1,6} (.+)$)"),
                                            QRegularExpression::MultilineOption);
    auto matches = heading.globalMatch(text);
    while (matches.hasNext()) {
        if (githubSlug(matches.next().capturedView(1)) == id) {
            return true;
        }
    }
    return false;
}

TEST(DocsAnchors, EveryTopicAnchorExistsInEveryLanguage) {
    for (std::size_t i = 0; i <= static_cast<std::size_t>(hwio::DocsTopic::Mcp); ++i) {
        const hwio::DocsPage page = hwio::docsPage(static_cast<hwio::DocsTopic>(i));
        for (const std::string_view language : hwio::docsLanguages()) {
            const std::string_view fileLanguage = page.translated ? language : "en";
            const QString path = pagePath(fileLanguage, page.page);
            const QString text = readPage(path);
            ASSERT_FALSE(text.isEmpty()) << "missing page " << path.toStdString();
            if (page.anchor.empty()) {
                continue;
            }
            const QString id = q(page.anchor);
            EXPECT_TRUE(hasExplicitAnchor(text, id) || hasHeadingAnchor(text, id))
                << path.toStdString() << " has no anchor " << page.anchor;
            // ADR-0259: the translated pages carry the explicit, language-independent id.
            if (page.translated) {
                EXPECT_TRUE(hasExplicitAnchor(text, id))
                    << path.toStdString() << " has no <a id=\"" << page.anchor << "\">";
            }
        }
    }
}

TEST(DocsAnchors, EveryErrorIdHasAnAnchorInEveryLanguage) {
    for (const std::string_view language : hwio::docsLanguages()) {
        const QString path = pagePath(language, "troubleshooting");
        const QString text = readPage(path);
        ASSERT_FALSE(text.isEmpty()) << "missing page " << path.toStdString();
        for (const hwio::ErrorInfo& info : hwio::errorTable) {
            EXPECT_TRUE(hasExplicitAnchor(text, q(info.id)))
                << path.toStdString() << " has no <a id=\"" << info.id << "\">";
        }
    }
}

TEST(DocsAnchors, GitHubSlug) {
    EXPECT_EQ(githubSlug(u"No console (RTT) found"), QStringLiteral("no-console-rtt-found"));
    EXPECT_EQ(githubSlug(u"Nordic nRF Util (LTE modem firmware only)"),
              QStringLiteral("nordic-nrf-util-lte-modem-firmware-only"));
    EXPECT_EQ(githubSlug(u"SEGGER J-Link Software"), QStringLiteral("segger-j-link-software"));
}

} // namespace

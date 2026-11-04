// Plan 0037 (ADR-0247, ADR-0259): the documentation URL table - GitHub at the release tag, the
// UI language with English fallback, `main` for untagged builds.

#include "hwio/core/docs_urls.h"
#include "hwio/core/error.h"
#include "hwio/core/version.h"

#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QtTypes>

#include <gtest/gtest.h>

#include <cstddef>
#include <string_view>

namespace {

using hwio::docsPage;
using hwio::docsRef;
using hwio::DocsTopic;
using hwio::docsUrl;
using hwio::errorDocsUrl;

QString q(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

TEST(DocsUrls, RefForReleasesAndUntaggedBuilds) {
    EXPECT_EQ(docsRef("2.0.0"), "v2.0.0");
    EXPECT_EQ(docsRef("2.0.0-rc.3"), "v2.0.0-rc.3");
    EXPECT_EQ(docsRef("2.0.0-dev+abc"), "main");
    EXPECT_EQ(docsRef("2.1.0+g1234"), "main");
    EXPECT_EQ(docsRef(""), "main");
    // The forms of cmake/HWioVersion.cmake for builds after a tag.
    EXPECT_EQ(docsRef("2.0.1-dev.5+abc1234"), "main");
    EXPECT_EQ(docsRef("2.0.0-rc.1.dev.5+abc1234.dirty"), "main");
    EXPECT_EQ(docsRef("2.0.0-dev"), "main");
    // Never anything but a version in the URL path.
    EXPECT_EQ(docsRef("2.0.0/../x"), "main");
    EXPECT_EQ(docsRef("2.0.0 rc"), "main");
}

TEST(DocsUrls, LanguageFallback) {
    const QUrl de = docsUrl(DocsTopic::Monitor, u"de_AT", "2.0.0");
    EXPECT_EQ(de.toString(),
              QStringLiteral("https://github.com/hardwario/monitor/blob/v2.0.0/docs/de/index.md"));
    EXPECT_EQ(docsUrl(DocsTopic::Monitor, u"de-DE", "2.0.0"), de);
    EXPECT_EQ(docsUrl(DocsTopic::Monitor, u"DE", "2.0.0"), de);
    EXPECT_EQ(docsUrl(DocsTopic::Monitor, u"pt", "2.0.0").path(),
              QStringLiteral("/hardwario/monitor/blob/v2.0.0/docs/en/index.md"));
    EXPECT_EQ(docsUrl(DocsTopic::Monitor, u"", "2.0.0").path(),
              QStringLiteral("/hardwario/monitor/blob/v2.0.0/docs/en/index.md"));
    EXPECT_EQ(docsUrl(DocsTopic::Monitor, u"system", "2.0.0").path(),
              QStringLiteral("/hardwario/monitor/blob/v2.0.0/docs/en/index.md"));
    // English-only reference pages.
    EXPECT_EQ(docsUrl(DocsTopic::Cli, u"cs", "2.0.0").path(),
              QStringLiteral("/hardwario/monitor/blob/v2.0.0/docs/en/cli.md"));
    EXPECT_EQ(docsUrl(DocsTopic::ConsoleBatch, u"cs", "2.0.0-rc.3").toString(),
              QStringLiteral("https://github.com/hardwario/monitor/blob/v2.0.0-rc.3/docs/cs/"
                             "console.md#batch-files"));
}

TEST(DocsUrls, EveryTopicIsAnHttpsGitHubLink) {
    for (std::size_t i = 0; i <= static_cast<std::size_t>(DocsTopic::Mcp); ++i) {
        const auto topic = static_cast<DocsTopic>(i);
        const hwio::DocsPage page = docsPage(topic);
        EXPECT_FALSE(page.page.empty()) << i;
        for (const std::string_view language : hwio::docsLanguages()) {
            const QUrl url = docsUrl(topic, q(language), "2.0.0");
            EXPECT_TRUE(url.isValid()) << i;
            EXPECT_EQ(url.scheme(), QStringLiteral("https"));
            EXPECT_EQ(url.host(), QStringLiteral("github.com"));
            const std::string_view expectedLanguage = page.translated ? language : "en";
            EXPECT_EQ(url.path(), QStringLiteral("/hardwario/monitor/blob/v2.0.0/docs/") +
                                      q(expectedLanguage) + u'/' + q(page.page) +
                                      QStringLiteral(".md"));
            EXPECT_EQ(url.fragment(), q(page.anchor));
            EXPECT_EQ(url.hasFragment(), !page.anchor.empty());
        }
    }
    // The default version is the running app's.
    EXPECT_EQ(docsUrl(DocsTopic::Monitor, u"en"),
              docsUrl(DocsTopic::Monitor, u"en", hwio::buildInfo().version));
}

TEST(DocsUrls, ErrorLinks) {
    const QUrl url = errorDocsUrl("probe.not_found", u"sk", "2.0.0");
    EXPECT_EQ(url.toString(),
              QStringLiteral("https://github.com/hardwario/monitor/blob/v2.0.0/docs/"
                             "sk/troubleshooting.md#probe.not_found"));
    for (const hwio::ErrorInfo& info : hwio::errorTable) {
        EXPECT_EQ(errorDocsUrl(info.id, u"en").fragment(), q(info.id));
    }
    EXPECT_TRUE(errorDocsUrl("no.such_error", u"en").isEmpty());
    EXPECT_TRUE(errorDocsUrl("", u"en").isEmpty());
}

} // namespace

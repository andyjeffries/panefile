// pf-syntax: KSyntaxHighlighting for Quick Look's text renderer (§2, §7.6).

#include "plugins/PluginInterfaces.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>

#include <QObject>
#include <QTextDocument>

namespace pf::plugins {

class SyntaxHighlightingPlugin : public QObject, public SyntaxPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PF_SYNTAX_PLUGIN_IID)
    Q_INTERFACES(pf::plugins::SyntaxPlugin)

public:
    QSyntaxHighlighter *attach(QTextDocument *document, const QString &fileName,
                               const QString &mimeName, bool dark) override
    {
        // The file name first: it is what distinguishes a CMakeLists.txt from
        // any other text/plain, and a Makefile from a file with no extension.
        KSyntaxHighlighting::Definition definition = m_repository.definitionForFileName(fileName);
        if (!definition.isValid()) {
            definition = m_repository.definitionForMimeType(mimeName);
        }
        if (!definition.isValid()) {
            return nullptr;
        }

        auto *highlighter = new KSyntaxHighlighting::SyntaxHighlighter(document);
        highlighter->setTheme(
            m_repository.defaultTheme(dark ? KSyntaxHighlighting::Repository::DarkTheme
                                           : KSyntaxHighlighting::Repository::LightTheme));
        highlighter->setDefinition(definition);
        return highlighter;
    }

private:
    /// Loading the definition repository is the "tens of milliseconds" §3.4
    /// keeps off the startup path. It happens once, when this plugin is first
    /// loaded, which is when the first text file is shown.
    KSyntaxHighlighting::Repository m_repository;
};

} // namespace pf::plugins

#include "SyntaxPlugin.moc"

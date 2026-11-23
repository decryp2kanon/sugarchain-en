Translations
============

Sugarchain En supports multiple localisations. Fork-specific strings and translations are maintained through pull requests in the [Sugarchain En repository](https://github.com/decryp2kanon/sugarchain-en).

### Helping to translate Sugarchain En
Use the local Qt translation files for this fork and submit reviewed changes through a pull request. The upstream translation service described below belongs to Bitcoin Core.

Translations make Sugarchain En accessible in more languages. Product names in user-facing strings should identify Sugarchain En, while historical attribution and technical identifiers keep their original meaning.

The [Transifex Bitcoin project](https://explore.transifex.com/bitcoin/bitcoin/) is an upstream reference, not a Sugarchain En translation service. The inherited `.tx/config` and upstream import tooling do not establish automatic synchronisation for this fork. Do not upload Sugarchain En strings to the Bitcoin project; review any upstream imports before proposing them here.

### Writing code with translations
We use automated scripts to help extract translations in both Qt, and non-Qt source files. It is rarely necessary to manually edit the files in `src/qt/locale/`. The translation source files must adhere to the following format:
`bitcoin_xx_YY.ts or bitcoin_xx.ts`

`src/qt/locale/bitcoin_en.ts` is treated in a special way. It is used as the source for all other translations. Whenever a string in the source code is changed, this file must be updated to reflect those changes. A custom script is used to extract strings from the non-Qt parts. This script makes use of `gettext`, so make sure that utility is installed (ie, `apt-get install gettext` on Ubuntu/Debian). Once this has been updated, `lupdate` (included in the Qt SDK) is used to update `bitcoin_en.ts`.

To automatically regenerate the `bitcoin_en.ts` file, run the following commands:
```sh
cmake --preset dev-mode -DWITH_USDT=OFF -DENABLE_IPC=OFF
cmake --build build_dev_mode --target translate
```

**Example Qt translation**
```cpp
QToolBar *toolbar = addToolBar(tr("Tabs toolbar"));
```

### Creating a pull-request
Include the source and translation changes needed for Sugarchain En in a focused pull request. Preserve placeholders, plural forms, shortcuts, and rich-text markup, and check that the generated translations still build.

Merging a change in this repository does not imply that the upstream Transifex project will import it. Reviewers should inspect the local translation diff and verify that upstream branding has not replaced Sugarchain En strings.

To create the pull-request, use the following commands:
```
git add src/qt/bitcoinstrings.cpp src/qt/locale/bitcoin_en.ts
git commit
```

### Upstream Transifex reference
Visit the [Transifex Signup](https://app.transifex.com/signup/open-source/) page to create an account. Take note of your username and password, as they will be required to configure the command-line tool.

The upstream Bitcoin translation project is available at [https://explore.transifex.com/bitcoin/bitcoin/](https://explore.transifex.com/bitcoin/bitcoin/).

### Installing the Transifex client command-line tool
The client is used to fetch updated translations. Please check installation instructions and any other details at https://developers.transifex.com/docs/cli.

The inherited `.tx/config` points to the upstream Bitcoin project. Keep it separate from fork-specific translation work; it is not a Sugarchain En publishing destination.

### Synchronising translations

An upstream import helper is available in the [Bitcoin Core maintainer-tools repository](https://github.com/bitcoin-core/bitcoin-maintainer-tools). The command below is upstream reference material; its output must be reviewed and adapted before inclusion in Sugarchain En:

```
python3 ../bitcoin-maintainer-tools/update-translations.py
git commit -a
```

**Do not directly download translations** one by one from the Transifex website, as we do a few post-processing steps before committing the translations.

### Handling Plurals (in source files)
When new plurals are added to the source file, it's important to do the following steps:

1. Open `bitcoin_en.ts` in Qt Linguist (included in the Qt SDK)
2. Search for `%n`, which will take you to the parts in the translation that use plurals
3. Look for empty `English Translation (Singular)` and `English Translation (Plural)` fields
4. Add the appropriate strings for the singular and plural form of the base string
5. Mark the item as done (via the green arrow symbol in the toolbar)
6. Repeat from step 2, until all singular and plural forms are in the source file
7. Save the source file

### Translating a new language
To create a new language template, you will need to edit the languages manifest file `src/qt/bitcoin_locale.qrc` and add a new entry. Below is an example of the English language entry.

```xml
<qresource prefix="/translations">
    <file alias="en">locale/bitcoin_en.qm</file>
    ...
</qresource>
```

**Note:** that the language translation file **must end in `.qm`** (the compiled extension), and not `.ts`.

### Questions and general assistance

For Sugarchain En translation questions, use the [project issues](https://github.com/decryp2kanon/sugarchain-en/issues). The [Bitcoin translators mailing list](https://groups.google.com/forum/#!forum/bitcoin-translators) is an upstream reference only.

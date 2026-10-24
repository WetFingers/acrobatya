# Карта кода «АкроБати»

Где что лежит и как добавляется типовая вещь. Сессия читает этот файл **первым**, до поиска по коду.
Дерево и состав — эталон `docs/design.md` 5.1–5.3; здесь — короткий путеводитель и рецепты.

> **Состояние: сессия 4.6 — интерфейс подписей: инструмент, диалоги, панель «Подписи», Установки → Подписи; версия
> 0.4.0** (этап 4 закрыт; запись журнала восстановил оператор — сессия остановилась по лимиту ходов).
> `ui/tools/certificate/` — `CertificateTool` (панель и меню «Подпись»: `sign.digital`, `sign.certify`,
> `sign.certifyInvisible`, `sign.timestamp`, `sign.validateAll`, `sign.identities`; `blockReason`,
> `startSign`/`startCertify`/`startTimestamp`, `placeSignature`, `verifyAll`, `offerLtv`), `SignPlacementTool`
> (`ViewToolId::Sign`, `kViewToolCount` 9), `SignDialog`/`CertifyDialog`, `NewIdentityDialog`, `AppearanceDialog`,
> `WarningsDialog`, `TimestampDialog`, `SignTexts`, `sign_script` (`--script` `sign.sign`, `sign.certify`,
> `sign.timestamp`, `sign.verifyAll`, `sign.trust`); `ui/shell/signature_status` (`SignatureStatus` — `DocumentView::signatures()`:
> проверка при открытии, после записи и смены доверия, полосы, значки `SignatureBadges`, чип «Подписано»),
> `ui/panes/signatures_pane` (`SignaturesPane`), `ui/dialogs/` — `signature_properties_dialog`, `certificate_viewer`,
> `trust_dialog`; `ui/shell/preferences/signatures_page` (`SignaturesPage`) + `identities_widget`, `trusted_widget`,
> `tsa_widget`; `MainWindow::openSignedVersion`, `checkTrustListsLater`. Службы — `crypto_worker.h` (`CryptoWorker`,
> `VerifyRequest`), `sign_ops.h` (`signing::*`), `sign_settings.h` (внешние виды JSON), `trust_service.h`
> (`TrustService::snapshot/edit`, сигнал `changed`); core — `sig_fields.h` (`signatureFields`, `emptyFieldAt`,
> `clearBlock`/`clearSignature`), `x509_info.h` — `certDetails`, `verifier.h` — `VerificationTime`
> (`VerifyOptions::time`); ключи `signatures/` — `showReason`, `showLocation`, `reasons`, `appearances`,
> `appearance`, `reviewWarnings`, `verifyOnOpen`, `revocation`, `acceptExpiredTimestamps`, `promptTrust`,
> `verifyTime`; ctest `unit.sig_fields`, `unit.sig_clear`, `unit.cert_details`, `unit.verifier_time`, `ui.sign_dialog`,
> `ui.signatures_pane`, `ui.sign_settings`, `ui.script_sign`, `ui.shots.stage4`; `tests/ui/support/sign_ui.h`,
> `tests/scripts/sign.json`; сцены `sign-*`, `certify-dialog`, `sig-bar-*`, `pane-signatures`, `certificate-viewer`,
> `preferences-identities`, `-trusted`, `-tsa`, `-verification`, `-sign-creation` (+ пять `*-dark`). Рецепт
> «Интерфейс подписей».
> Из 4.5 — LTV, DocTimeStamp, сертификация DocMDP, EUTL и AATL.
> `core/sign/` — `ltv.h` (`collectLtv(doc, LtvOptions{trust, fetcher, vri, now})` → `LtvCollection
> {signatures (LtvSignature: field, kind, vriKey, material, chainComplete, revocationComplete, missing), material,
> existing, added()}`, `writeDss`, `addValidationInfo(doc, options, target)` → `LtvResult{collected, dss, save}` —
> /DSS отдельной секцией; `ltvStatus`/`ltvEnabled` — «LTV включено» без сети; `ocspResponseDer`; `extendArchiveTimestamp`
> → `ArchiveResult` — продление B-LTA; `LtvAutoAdd`/`decideAutoLtv` — вопрос 12.3), `signer.h` — `SignLevel` B/T/LT/LTA,
> `SignRequest::level/trust/revocationFetcher/vri/certify`, `SignResult::level/dss*/ltv*/docTimestamp*/archiveError`,
> `SignStatus::CannotCertify`; `docmdp.h` — `writeDocMdp`, `certifyBlock`, `mdpAllows(MdpPermission, MdpAction)`;
> `warnings_scan.h` (`scanWarnings(doc)` → `WarningScan{warnings (DocumentWarning: kind, level, page, object, where,
> detail)}`, `WarningKind`, `WarningLevel`); `eutl.h` (модели `Lotl`/`ListPointer`/`TrustedList`/`TrustService`,
> `TrustListSource`, `LotlPins`, `usableService`, `buildEutl` → `EutlResult{pinned, lotlExpired, territories,
> certificates, trustworthy()}`), `aatl.h` (`parseAatl/parseAatlFile` → `AatlResult{status, certificates, signedList,
> signatureIntact, signer}`, порог `kAatlMinCertificates`), `trust_store.h` — `ListedCertificate`, `replaceSource`,
> `TrustAnchor::label`, `importWindows`, `windowsStoreCertificates(WindowsStore::Root|Ca)`, `exportAnchor`/`exportAll`,
> `windowsRootsFlags`, `trustFromSignature`; `verifier.h` — `certifiedPrivileges`; `revocation.h` — `archivalRevocation`;
> `ossl.h` — `base64Encode/Decode`; `sign_field.h` — `detail::needsPreSave`. Службы — `DocumentSession::addValidationInfo/
> extendTimestamp`, `trust_lists.h` (`parseLotl`/`parseTrustedList` на QXmlStreamReader, `lotlPins()` — ресурс
> `:/trust/lotl_pins.json` из `assets/trust`, `HttpTrustListSource`, `downloadTrustLists` → `TrustListUpdate`, кеш
> `trust/{eutl,aatl}.json`, `installTrustLists`), `trust_updater.h` (`TrustUpdater::check/install/discard`, раз в 14
> дней, `setClock`); ключи `signatures/autoAddLtv`, `signatures/ltvVri`, `trust/eutl`, `trust/aatl`,
> `trust/listsAskBeforeInstall`, `trust/listsCheckedAt`; фикстуры `tests/fixtures/trust/` (`make_trust_fixtures.py` из
> снимка корпуса), `tests/fixtures/sig/warnings.pdf` (`make_warnings.py`); ctest `compat.sign.lt`, `compat.sign.lta`
> (токены `LTV`, `LTA` в `compat_check.py`), `engine.aatl`, `compat.trust.live` (только `OTTISK_LIVE_NET=1`).
> Из 4.4 — валидатор: статусы Acrobat, анализ изменений, DSS-корпус. `core/sign/` — `verifier.h` (`verifyDocument(doc, VerifyOptions{trust, fetcher, revocation
> Required/BestEffort/Never, retroactiveRevocation, acceptExpiredTimestamps, now})` → `DocumentVerification{signatures,
> status (DocumentStatus — полоса), versions, embeddedCertificates, docMdp}`; `SignatureInfo` — поле, страница и рамка,
> /V, /ByteRange, CMS, подписант, `structureOk`/`intact`/`trusted`, `chain`, `validationTime` + `TimeSource`,
> `revocation`/`revocationDetails`/`ltv`, `timestamp`/`timestampIntact`/`timestampTrusted`, `PadesLevel`, `docMdp`/`locks`/
> `changes`, `Verdict` (Valid, ValidWithChanges, ValidUntrusted, Unknown, Invalid) и `findings` (`Issue` + английская
> подробность, `issueSeverity`); два прохода: структура и целостность всех подписей, затем доверие по общему пулу
> сертификатов документа), `trust_store.h` (`TrustStore` — якоря с флагами `kTrust*`, источник, отпечатки SHA-1/SHA-256,
> `import(.cer/.p7c/.fdf)`, `load/save` anchors.json, `useWindowsRoots`, `x509Store(flags)`; `windowsRootCertificates`),
> `docmdp.h` (`readDocMdp`, `signatureDocMdp`, `fieldLocks`, `isFieldLocked`, `MdpPermission`), `changes.h`
> (`analyzeChanges(doc, signedEnd, ChangeRules)` → `ChangeReport{modified, level, allowed, changes}`, `ChangeKind`,
> `ChangeLevel`, `permittedLevel`); `CmsInfo::signingCertificateV1`; тесты — `support/verify_support.h` (`dssFile`,
> `verifierExpectations`, `expectSignature`, `patchContents`, `pkiResponder`, `offlineFetcher`, `revokedIdentity`,
> `embeddedRoots`, `certify(file, P)`, `editIncrementally`, `setTextField`, `addTextNote`), ожидания —
> `tests/fixtures/sig/verifier_expected.json` и `dss_expectations.json`; ctest `engine.sig.dss`, `compat.sig.pyhanko`;
> `tools/compare_pyhanko.py` (survey/expect/compare).
> Из 4.3 — CNG-подписант: хранилище Windows и внешний PKCS7-путь (интерфейса подписей
> ещё нет — 4.6). `core/sign/` — `cng_store.h` (`cng::CertStore` — «Личное» только чтение / по команде, память,
> `importPfx` с ключом в памяти процесса (`KeyStorage::Memory`) или в KSP; `CertContext` — RAII над `PCCERT_CONTEXT`;
> `list(CertFilter)` → `CertEntry{thumbprint, summary, hasPrivateKey, signingUsage, expired, keyProvider, smartCard}`,
> `find(отпечаток)`, `filtered`, `importIdentity`, `normalizeThumbprint`), `cng_signer.h` (`CngKey::acquire/signHash` —
> RSA PKCS#1/PSS, ECDSA P1363 → DER, старый CSP; `CngError::Status`; `CngIdentity::open(store, отпечаток, CngOptions{window,
> silent})`; `certificateChain`; `cngCmsFromDigest`), `cms_external.h` (`cadesSignedAttributes`/`essSigningCertificateV2` —
> атрибуты своим DER, `ExternalCmsBuilder(cert, chain, ExternalSignFn)` — PKCS7-API с внешней подписью, тот же DER, что
> `CmsBuilder`), в `der.h` — запись (`encode`, `join`, `encodeSetOf`, `encodeOid`); `signer.h` — `SigningIdentity` =
> variant{.pfx, `WindowsCert{thumbprint, window, silent, store}`}, `openWindowsIdentity`, `SignStatus::NoPrivateKey/
> Cancelled/KeyFailed`; службы — `cert_picker.h` (`CertPicker::available/pick` — системный диалог только на экране);
> ctest `compat.sign.cng`, `engine.sign.matrix`; страж `boundaries` — Windows SDK не в заголовках core.
> Из 4.2 — подписание B-B/B-T, DocTimeStamp, внешний вид и удостоверения. `core/sign/` — `signer.h` (`PdfSigner` — `pdf_pkcs7_signer` поверх «хэш /ByteRange → DER»,
> резерв 16 384; `AcroBatyaSigner` — CMS CAdES-B + метка B-T; `signDocument(doc, SignRequest, target)` → `SignResult`,
> `SignError`/`SignStatus`), `sign_field.h` (общий ход подписи поля: `detail::FieldJob{placement, lock, appearance,
> before, patch}`, `signField`, `ensureEsic`, `nextFieldName`; патч /V — в секции подписи), `doc_timestamp.h`
> (`TimestampSigner`, `addDocumentTimestamp`), `tsa_servers.h` (`TsaServers` — DigiCert по умолчанию, JSON,
> `checkTsaServer`, `TimestampFallback`), `appearance.h` (`SignatureAppearance` + `fromFlags`, `AppearanceData`,
> `appearanceLines`, `buildAppearance` — глифы контурами, `previewAppearance`), `identity.h` (`createSelfSigned`,
> `exportPkcs12`, `importToWindows`, `exportCertificate` .cer/.p7c/.fdf, `readFdfCertificates`, `IdentityStore` над
> state.sqlite + `SecretProtector`); `Saver::revert`; службы — `DocumentSession::sign`/`timestampDocument`,
> `tsa_settings.h`, `dpapi.h` (+`DpapiProtector`), `identities.h` (`Identities::shared().with(fn)`), миграция 5
> state.sqlite, ключи `signatures/`; ctest `compat.sign.{b,b.multi,t,dts}`; `tools/compat_check.py --expect-ts`.
> Из 4.1 — крипто-ядро подписей, тестовая PKI и локальный TSA.
> `core/sign/` — `ossl.h` (`Ossl<T>` — RAII по типу, `OsslError::fromQueue`, `ossl::check/checkPtr`, `memBio`/`bioBytes`,
> `openRangeBio` — BIO по `/ByteRange` файла, `sha256Ranges`, `toDer`, `toHex`, `ensureLegacyProvider`; `cmake/openssl.cmake` —
> граница ≥ 3.5), `der.h` (свой DER/BER-разбор: `Reader`, `toOid`, `toInt`, `toTime`), `x509_info.h` (`CertSummary`,
> `summarize`, `nameToString` RFC 2253 в UTF-8, `loadCertificate(s)`), `pkcs12.h` (`loadPkcs12(байты, пароль UTF-8)` →
> `SigningKey{key, cert, chain, friendlyName}`, `Pkcs12Error`/`Pkcs12Status`), `cms.h` (`CmsBuilder` — CAdES-B detached:
> `build(BIO)`/`buildFromDigest`, `signatureValue`, `addUnsignedTimestamp`; `cmsVerifyDetached`; `parseCms` → `CmsInfo`
> своим DER-разбором; OID — `oid::k*`, `algorithmName`), `tsa.h` (`makeTsaRequest`, `checkTsaResponse`,
> `verifyTimestampToken`, `parseTimestampToken` → `TimestampInfo`, `TsaClient{fetcher, TsaConfig{url, user, password}}`),
> `chain.h` (`ChainBuilder(store, time, partialChain)` → `ChainResult{status, chain, certs, fetched}`, `ChainPurpose`,
> `CrlCheck`, AIA caIssuers; `ocspUrls`/`caIssuersUrls`/`crlUrls`), `revocation.h` (`RevocationChecker(RevocationData,
> fetcher)` — OCSP, затем CRL, `setTime`/`setRetroactive`, `check`/`checkChain` → `RevocationResult`, `worstStatus`);
> `core/util/http_fetcher.h` (`net::HttpFetcher`, `Request`/`Response`/`Status`/`Purpose`, `FakeHttpFetcher`),
> `services/http_fetcher.h` (`QtHttpFetcher`, `NetworkAccess` по `trust/network`, `httpFetcher()`, `ScopedHttpFetcher`;
> страж `boundaries` — сеть только там); `tools/pki/` и `tests/fixtures/pki/` (PKI, `tsa_serve.py`), тесты —
> `support/pki.h`, `support/tsa_server.h`; ctest `compat.tsa.public` (только `OTTISK_LIVE_NET=1`).
> Из 3.4 — инструмент «Редактировать PDF», версия 0.3.0 (этап 3 закрыт). `ui/tools/edit/` —
> `EditTool` (панель: «Текст», «Добавить текст», «Добавить изображение», `FormatPanel`, «Объекты», серые пункты
> 0.7; причина запрета — подпись, `/Perms`, только чтение, права), `EditMode` — инструмент вида в слоте
> `ViewToolId::Edit` (`ToolController::install`; рамки блоков и картинок, каретка, IME, локальная копия абзаца,
> запись `editText` по выходу из блока/Esc/паузе 1,5 с, очередь «отложенных» записей; `add_text.cpp` — новый
> блок и маркеры; `image_tool.cpp` — выбор, маркеры, меню, обрезка, буфер обмена, «Редактировать в…»;
> `caret.cpp` — клавиши и IME; `edit_overlay.cpp` — слой), `edit_script.cpp` (`edit.insertText`,
> `edit.replaceAll`); `core/edit/images.h` (`ImageOps` — группа места `q cm [re W n] Do Q`, `pageImages`,
> `probeImage`), `core/ocr/is_scan.h` (`checkScan`); `services/edit_ops.h` (`pageContent` — блоки с
> `ParagraphEditor` и картинки, `newParagraph`, правки картинок шагами истории, `lock`), `services/ocr_ops.h`
> (`scanCheck`), `DocumentSession::findReplace/replaceHit/replaceAll`; `ui/dialogs/replace_dialog` (предпросмотр
> и отчёт «Заменить все»), поле «Заменить на» в `FindBar` (Ctrl+Shift+H); «Установки → Редактирование
> содержимого» (`EditingPage`, ключи `editing/`), `Tool::settingsChanged`; полоса «Похоже на скан»; сцены
> `edit-*`, `find-replace`, `scan-bar`, `preferences-editing`. Из 3.3 — редактор абзаца, перевёрстка и запись
> потока. `core/edit/` —
> `paragraph_editor.h` (`ParagraphEditor::fromBlock` — строка символов `EditChar{ch, style, code, w0, group, glyph}`
> с пробегами `CharStyle`, `insert/erase/replace/applyStyle/setAlignment/setLineSpacing`, `ParagraphGeometry`,
> `ParagraphSource{page, ops, text, bbox}`, `TextLayout` и курсор `caret/positionAt/moveLine/wordAt/selectionQuads`,
> `Script`/`scriptChange`), `reflow.h` (`ReflowChar`, `ReflowParams`, `reflow` → `TextLayout` + `overlaps`,
> `reflowParams`, `naturalAdvance`, `toLayoutRects`), `rewriter.h` (`Rewriter::rewrite/layout/subsetFonts`,
> `RewriteReport`, `RewriteError`), `replace.h` (`Replacer::find/findAll/replaceOne/replacePage/replaceAll`,
> `ReplaceMatch`, `ReplaceSkip`), `edit_types.h` (`Align`, `FontNotice`, `FallbackFonts` — без MuPDF); модель —
> `StateRefs` у спана, `opText`, `colorOps`, `replace(op, bytes)`; `text_blocks.h` — `underlineMarks`;
> `services/document_session.h` — `editText`/`layoutText`/`setEditFallback`, `EditTextResult`;
> `render/image_compare.h` — `diffPixelsOutside`, `pixelRect`; `engine.edit_render.*` (отчёт `build/edit-render.json`),
> `compat.edit` (`tools/compat_check.py --expect-text`). Из 3.2 — шрифты редактора. `core/edit/` — `font_info.h` (`FontProbe` —
> шрифт ресурса: `FontDetails` (тип, подмножество, кодировка, программа, /Flags, /ItalicAngle, /StemV, вес,
> курсив, засечки) и `encode(U)` — код в кодировке шрифта с непустым глифом; `parseFontName` — семейство и стиль
> из PostScript-имени и имени Word), `system_fonts.h` (интерфейс `SystemFonts`, `findSystemFont` — имя грани →
> семейство → метрическая замена base14, `pickFace`, `FakeSystemFonts` по `C:\Windows\Fonts`), `font_embed.h`
> (`FontLibrary` — системные файлы, `FontEmbedder::add(page, face, Cid|SimpleWinAnsi)` — новый шрифт с ресурсом
> `OtFn`, `encode`/`width`; `subsetFonts` — подмножества только своих шрифтов, `SaveOptions::prepare` в Saver),
> `font_resolver.h` (`FontResolver::resolve/runs` — четыре правила эталона 3.9, `FontChoice`, сообщения
> `FontNotice`, `FallbackFonts`; синтетика `syntheticStyle/TextMatrix/Ops`); `services/system_fonts_dwrite.h`
> (`DWriteSystemFonts`, `systemFonts()`, `editFallbackFonts(Settings)` — ключ `editing/fallbackFont`); фикстуры
> `tests/fixtures/edit/fonts/` (`tools/gen/make_font_fixtures.py`, `fonts.json`); `compat.fonts`,
> `engine.font_mixed.*`, `ui.dwrite`. Из 3.1 — модель контент-потока, абзацы и стабильность: `core/edit/` —
> `content_model.h` (`ContentModel::build(doc, page)`: свой токенизатор на `pdf_lex` + `fz_tell` — операции
> `Operation{kind, name, operands, stream, offset, length, tokenOffset, split}` с исходными байтами
> декодированного потока, графическое и текстовое состояние как у интерпретатора MuPDF, формы разворачиваются на
> месте (только чтение), BI…EI через `pdf_load_inline_image`, выключенные слои `/OC`; глифы с Unicode, ширинами,
> позициями и рамками как у fz_stext; `TextSpan` — один на оператор показа, причина «только чтение» `ReadOnly`;
> `serialize(stream)` — байт в байт; `rebuildParagraph(block)` + `apply(doc)` — «пустая правка» и запись
> потоков операцией журнала со сжатием; `crossCheck` — сверка с fz_stext через `pdf_new_run_processor`),
> `glyph_map.h` (`GlyphMap` — коды → глифы как show_string/pdf_show_char, `FontInfo`), `text_blocks.h`
> (`blocksForPage` → `TextBlock{lines, spans, runs, text, align, firstIndent, leading, column, bbox, reason}`,
> `boundingRect()`), `empty_edit.h` (`emptyEditPage` — метрика стабильности); фикстуры `tests/fixtures/edit/`
> (`tools/gen/make_edit_fixtures.{ps1,py}`, ожидания `expected.json`); корпус `text-edit` — `engine.text_edit/
> ContentRoundtrip.*` и `engine.edit_stability.*` (отчёт `build/edit-stability.json`).
> Из 2.6 — печать, версия 0.2.0; рубеж Р1 пройден (`tools/verify_r1.py`; рамки флажков и
> переключателей — `IndicatorStyle` поверх Fusion в `ui/theme/theme.cpp`; `MemoryGuard` в `MainWindow`). `core/print/` — `page_set.h` (какие страницы: диапазон с
> логическими номерами, чётные/нечётные, обратный), `imposition.h` (листы: один на листе, n-up, буклет, постер;
> `Paper`, `Placement`, `placementMatrix`), `band_renderer.h` (display list usage "Print" — `/PrintState` и пометки по
> флагу Print, «Документ / и пометки / только поля»; полосы ≤ 32 МиБ, серый — `PixelFormat::Gray8`);
> `services/print_job.h` (`PrintDevice`: QPrinter, QPdfWriter, GDI «в файл» — `print_job_gdi.cpp`, `FakePrintDevice`;
> `PrintDevices` + `ScopedPrintDevices`/`demoPrinters` и `OTTISK_FAKE_PRINTER` — без живых принтеров; `makePrintPlan`,
> `printDpi`, `PrintJob::run/preview`, `printFileSilently`); `ui/tools/print/` (`PrintDialog`, `PrintPreview`,
> `PrintRunner`, `PrintTool`), «Установки → Печать» (`print_page`), `MainWindow::showPrintDialog`/`printSilently`,
> `/p` и `/pt` (`cli.h` `runPrint`, `SingleInstance::Request::print`), «Печать страниц…» у миниатюр и закладок,
> `DocumentView::selectedPages`; ключи `print/`; токен `printPreview`; сцены `print-*`, `preferences-print`.
> Из 2.5b — защищённый просмотр, права `/P`, подтверждение ссылок, замена цветов, overprint,
> «Просмотр страницы» целиком. `services/protected_view.h` (Zone.Identifier, временные папки, вложения, «Включить все
> функции» — `state.sqlite` `trusted_files`), `core/doc/permissions.h` (действующие права), `services/link_policy.h` +
> `ui/shell/link_flow` + `ui/dialogs/link_confirm_dialog` (ссылки наружу), `core/render/render_style.h` +
> `recolor_device.h` (стиль рендера: AA, картинки, тонкие линии, overprint, замена цветов; поле `style` ключа тайла),
> `ui/view/view_style.h` (настройки → стиль), `core/pages/boxes.h` + `ui/view/boxes_overlay` (рамки областей),
> `Command::blocked` (серая команда с причиной), `services/sql.h` (`SqlStatement`); `state.sqlite` — миграция 4.
> Из 2.5 — поиск («Найти», «Расширенный поиск»), «Свойства документа», сохранение в UI.
> `ui/panes/find_bar` (Ctrl+F, `DocumentView::findBar()`), `ui/dialogs/search_window` (Ctrl+Shift+F), `services/search_ops.h`,
> `searchSnippet`, `TextIndex::suggest`; `ui/dialogs/properties_dialog` (Ctrl+D) + `xmp_editor`, `writeInitialView`/`setBaseUrl`
> (`core/links/initial_view.h`), `props::readSheet`/`apply`; `ui/shell/save_flow` (`SaveFlow`: Ctrl+S, «Сохранить как»),
> `ui/dialogs/{save_as,password,recover}_dialog`, `services/save_ops.h`, `AutosaveRunner` и `offerRecovery` в окне,
> `DocumentView::setReadOnly`/`canEdit` (PDF/A); сцены `find-bar`, `search-window`, `properties-*`, `xmp-editor`,
> `password-dialog`, `recover-dialog`, `pdfa-bar`.
> Из 2.4b — Установки, палитра команд, `--script`, `--open-all`, полный `--self-check`.
> `services/settings.h` + `settings_keys.h` (все ключи settings.ini и умолчания; `Settings::get/set` по BoolKey/IntKey/
> StringKey/ListKey, `installDefaultFormat`, `migrate` — `meta/version`), `ui/shell/preferences/` (диалог «Установки»
> Ctrl+K: 16 категорий эталона 3.18, `PreferencesPage`), `ui/shell/command_palette` (Ctrl+Shift+P), `ui/widgets/row_delegate`;
> `services/script_runner.h` + `ui/app/script_actions.h` (`--script`, базовые действия doc.*/view.*/shot), `Saver::saveCopy`,
> `services/props_ops.h`, `services/open_probe.h` + `--open-all`, `core/ocr/tessdata.h` (проба языка), `app::selfCheck`
> целиком; сцены `preferences-*`, `palette`.
> Из 2.4 — правая навигация, строка состояния, полосы сообщений, режим чтения и полный экран.
> `ui/shell/nav_pane` (полоса 44 + `NavPanel` 280 в сплиттере; `NavPage` — страница панели, заглушки «появится в 0.N»),
> `ui/panes/` (миниатюры, закладки, слои, вложения, назначения), `ui/shell/status_bar` (32 px), `ui/shell/message_bar`
> (`MessageStack` — виды полос по важности), `ui/shell/read_mode` (`ReadModeBar`, `MainWindow::setReadMode`),
> `ui/shell/full_screen` (`FullScreenWindow`, `FullScreenSettings`); ядро — `core/links/{outlines,destinations,attachments,
> layers,initial_view}.h`, `core/pages/labels.h`, `core/doc/presentation.h`; службы — `services/shell_launcher.h`
> (+`FakeShellLauncher`; страж `unit.guard.boundaries` — оболочка Windows только тут), `nav_ops.h`, `background_tasks.h`;
> `DocumentSession::layersHash()`/`layersChanged` (видимость слоёв в ключе тайлов), `OpenSummary::pageLabels`/`initial`.
> Из 2.3 — оболочка нового Acrobat, вкладки, второй экземпляр, Главная, ToolRegistry, реестр команд.
> `ui/shell/main_window` (GlobalBar 48 + левая `ToolPanel` 320/48 + `QSplitter` с центром — Главная / `DocumentView`
> на вкладку), `global_bar`, `document_tabs`, `document_view` (PageView, стек полос, QuickBar), `menus.cpp` (команды окна в
> реестре, меню из реестра), `tool_panel`, `quick_bar`, `home/` (Главная: недавние, избранное, файлы, миниатюры, «Краткие
> сведения»); `ui/tools/` — `Tool`, `ToolRegistry`, 16 заглушек `builtin_tools.cpp`; `services/commands.h` (реестр команд и
> клавиш, контексты `when`), `single_instance.h` (`QLocalServer`), `session_store.h`, `recent.h`, `thumbnails.h`,
> `convert_ops.h` + `core/create/to_pdf.h` (не-PDF → PDF); `state.sqlite` — миграции 2 и 3; `AcroBatya.exe --commands-list`
> и страж `unit.guard.hotkeys` (таблица эталона 6.8); сцены `shell-*`, `all-tools*`, `quick-bar`, `home-*`, `menu-open`.
> Плавающее окно «Лупы» из 2.2 теперь `ToolWindow`. Из 2.2b: `ui/view/tools/annot_select.cpp` — «Выбор объектов» (`AnnotSelectTool`: хит-тест `annotAt` задачей, ручки, `setRects`
> одним шагом, `Del`, Shift/рамка, сигнал `annotContextMenu`); `core/links/links.h` + `services/link_ops.h` +
> `ui/view/link_layer.h` — ссылки (курсор-указатель, переход по щелчку, Tab/Shift+Tab, Enter, `PageView::linkActivated`;
> Launch не исполняется); `PageView::setSearchHits`/`setCurrentSearchHit`/`ensureVisible`; Ctrl+C (text/plain + text/html
> нескольких страниц) и клавиатурный курсор `Shift+стрелки` у «Выделения»; сцены `view-*` (`ui/shots/view_scenes.cpp`,
> `Scene::ready`); `bench ui-paging` (ctest `bench.ui.paging`). Из 2.2 — виджет просмотра и минимальное ядро аннотаций. `ui/view`: `PageView`
> (`QAbstractScrollArea` над `layoutPages()` — пять режимов, зум 8–6400 % с якорем, подгонки, поворот вида, клавиши 6.8,
> история видов, автопрокрутка), `TilePainter` (display list задачей сессии → тайлы в `RenderPool` → `TileCache`, превью,
> подмена прежним масштабом, отмена, «Страница не отрисована»), `OverlayStack` (выделение, поиск, рамки аннотаций, слой
> инструмента), `ToolController` + инструменты просмотра (`tools/`: Рука, Выделение, Увеличение области, Лупа,
> Панорамирование и масштаб, Снимок; «закрепить»); `core/annot/annots.h` (`AnnotRef`, список/хит-тест/рамка/удаление с
> undo) и `services/annot_ops.h`; `OpenSummary::pageLayout`, сигнал `DocumentSession::structureChanged`. Из 2.1 — тема, иконки, компоненты, витрина, снимки и каркас i18n: `ui/theme` (токены трёх
> палитр и `Tokens::scaled`, `Theme::apply` без перезапуска, QSS одной таблицей правил, `Icons` — Lucide по теме),
> `ui/widgets` (15 компонентов и витрина `--showcase`), `ui/shots` (таблица `SCENES`, варианты темы/масштаба/языка),
> `services/i18n` (`.qm` ресурсом `:/i18n` с qtbase), `ui_tests` — наборы по файлам (ctest `ui.<имя>`).
> Из 1.4 — текст: слой, поиск, выделение, индекс FTS5 и экспорт; версия 0.1.0. `TextPage`
> (`core/text/stext.h`: блоки/строки/символы с рамками и стилем, слова, попадание в символ/строку/слово, векторы,
> структура) и `TextCache` (LRU 16) в `DocumentSession::textPage`; поиск `searchPage`/`SearchSession` (`fz_new_search`,
> «целое слово», счётчик «N из M»); своё выделение (`selection.h`) и `ClipboardIo` (текст + HTML, снимок PNG);
> `TextIndex` (FTS5) + `StateDb` (`state.sqlite`, миграции) + `FolderSearch`; экспорт TXT/HTML/XHTML/XML/RTF
> (`core/create/text_export.h`, `rtf_writer.h`, `DocumentSession::exportText`), ctest `compat.rtf` (Word COM).
> Из 1.3: сохранение, журнал undo/redo, автосохранение, ревизии и XMP. Журнал MuPDF включён с
> открытия (`Journal::Operation`, `UndoStack`; `DocumentSession::edit` идёт в операции, `undo`/`redo`, `undoState`),
> `Saver` (правила эталона 4.4, временный файл и замена, переоткрытие в том же `Document`), `QpdfTool`, `Autosave` +
> `AutosaveRunner` (свой загрузчик журнала), `revisions` (версии, подписанная ревизия), `props` + `xmp` (/Info ↔ XMP,
> /Lang, шрифты, «Дополнительно»), ctest `compat.save*`. Из 1.2: `MuContext::global()` (клоны на
> документ и поток, `forThisThread()`, `family()`), `MuError` с `ErrorKind`, `mu::call`, RAII `mu::*Handle`,
> `Document::open → OpenResult` (пароли, ремонт, PDF/A, подписи, форма, XFA, свойства, права), `Renderer` и
> `renderDisplayList`, `DisplayList`/`DisplayCache` (LRU 40), `tiles.h` (`TileKey`, одна матрица, тайлы 512, превью
> 1/8), `fit.h` (`fitZoom`, проценты, раскладка), `DocumentSession` на своём потоке (очередь с приоритетами,
> `post`/`edit`/`postSync` → `QFuture`, `cancelAll`, `TaskFailed`), `DocumentRegistry`, `RenderPool`, `TileCache`,
> `MemoryGuard`, `bench.exe` (Release) с базой `docs/bench/baseline-1.2.json`, `imgcmp --cross`, эталоны рендера
> бит в бит, `engine.corpus.*`/`engine.mutants.*`/`engine.stress.render`, фаззер через ядро. Из каркаса этапа 0:
> пустое окно `MainWindow` (Главная-заглушка, меню, строка состояния; страница — `DocumentSession::renderPage`), режимы
> `--version`, `--self-check`, `--shots`, `--smoke`, `--register-notify`, первые unit/ui/render-тесты, `imgcmp`,
> `render_refs`, стражи, фаззер, установщик 0.0.1. Всё остальное в таблицах ниже появляется в указанной сессии —
> рецепты описывают, как это будет устроено, и ссылаются на файлы, которых может ещё не быть. Сессия, которая
> создаёт новую типовую вещь или меняет её устройство, **дополняет этот файл**.

## Цели CMake (`CMakeLists.txt`, `cmake/*.cmake`)

| Цель | Каталоги (GLOB_RECURSE … CONFIGURE_DEPENDS) | PCH | Зависит от |
|---|---|---|---|
| `ottisk_core` (static) | `src/core/**` | `src/core/pch.h` (MuPDF + std) | MuPDF `.lib` (`cmake/mupdf.cmake`), OpenSSL, tiff, fmt, spdlog, json, sqlite3, Tesseract/Leptonica |
| `ottisk_services` (static) | `src/services/**` | `src/services/pch.h` (QtCore) | `ottisk_core`, Qt Core/Gui/Network/PrintSupport/Concurrent |
| `ottisk_ui_lib` (static) | `src/ui/**` кроме `app/main.cpp` | `src/ui/pch.h` (QtWidgets) | `ottisk_services`, Qt Widgets/Svg |
| `AcroBatya` (exe, WIN32) | `src/ui/app/main.cpp` + `installer/app.rc`, `installer/AcroBatya.manifest` | — | `ottisk_ui_lib` |
| `unit_tests` | `tests/unit/**` | — | gtest, `ottisk_services`; метка `unit` |
| `ui_tests` | `tests/ui/**` | — | QTest, `ottisk_ui_lib`; метка `ui` |
| `engine_tests`, `render_tests`, `e2e_tests` | `tests/engine/**`, `tests/render/**`, `tests/e2e/**` | — | gtest; метки `engine;slow`, `render;slow`, `e2e;slow` |
| `imgcmp`, `render_refs`, `bench` | `tools/imgcmp/**`, `tools/render_refs.cpp`, `tests/bench/**` | — | `ottisk_core` (`bench` — `ottisk_services`: тайлы через `RenderPool`) |
| `pdf_open_fuzzer` | `fuzz/pdf_open_fuzzer.cpp` | — | `ottisk_core` (с `/fsanitize-coverage`); только пресет `msvc-fuzz`, запуск — `tools\fuzz.ps1` |

Новый файл с `Q_OBJECT` после переконфигурации GLOB AutoMoc иногда не видит («нет `<имя>.moc`», неразрешённый
`staticMetaObject`) — удалить `build/debug/<цель>_autogen/timestamp` и собрать снова (2.3).

Все exe — в `build/<пресет>/bin/`. Qt DLL в `build` не копируются: кто запускает exe из `build`, добавляет
`C:\Qt\6.12.0\msvc2022_64\bin` в `PATH` (ctest делает это сам через `ottisk_test_env.cmake`). Версия — одна строка
`set(OTTISK_VERSION "0.N.0")` в `cmake/product.cmake`. Пресеты: `msvc-debug` (`build/debug`, рабочий),
`msvc-release` (`build/release`, рубежи), `msvc-asan`, `msvc-fuzz`, `msvc-analyze`.

**Новый `.cpp`/`.h` кладётся в каталог своей цели — CMake не правится.** Новая цель, каталог вне этих,
правка PCH или `CMakeLists.txt` — только главный исполнитель сессии (после правки PCH `verify.py` может
потребовать один раз `--target clean`).

## Где что лежит (эталон 5.2)

### `src/core/` — без Qt (страж `unit.guard.boundaries`: ни одного `<Q…>`)

| Каталог | Что | Сессия |
|---|---|---|
| `mu/` | `context.h` (`MuContext::global()` — один корень на процесс, `clone()` на документ и поток, `forThisThread()` — thread_local клон для потоков пула, `family()` — корень и клоны с общим store: объекты MuPDF ходят только внутри семьи, `shrinkStore`/`emptyStore`, замки, журнал MuPDF → spdlog), `error.h` (`MuError`, `ErrorKind`: Damaged/Unsupported/Limit/System/Aborted…), `call.h` (`mu::call` — `fz_try` в одном месте, `#pragma warning(disable: 4611)`), `handles.h` (RAII `mu::PageHandle`, `PixmapHandle`, `BufferHandle`… — объявлять снаружи `mu::call`, внутри только `reset`), `stream.h` | 0, 1.1 |
| `doc/` | 2.4 — `presentation.h` (`PageTransition` — `/Trans` и `/Dur` по словарю страницы без загрузки, `transitions(doc)`); `document.h` (`Document::open(path, password) → OpenResult`, `openOrThrow` для инструментов, свойства, `allowed(Perm)`, `pageBox`/`pageRotation`, `pdf()`, `undoStack()`, `setProperties`, `recoverFrom` (автосохранение), `path()`/`filePath()`/`backingFile()`; документ живёт в клоне базового контекста), `journal.h` (`Journal::Operation` RAII — вложенная присоединяется к внешней, `invalidate`/`invalidateAll`/`markHeavy`; `UndoStack` — шаги с именами поверх шагов MuPDF, группировка одноимённых за 1 с, `TouchedPages`, `hasHeavyChanges`, `clear`, слушатель), `saver.h` (`Saver::decide/save`, `SaveMode`, `SaveError`, `SaveOptions` — шифрование, `allowBreakingSignatures`, `linearize`; 2.4b — `saveCopy` «Сохранить как копию»: второй документ из снимка журнала (`Autosave` во временной папке), документ остаётся на своём файле с правками и историей, `SameAsDocument`; 2.5 — `SaveOptions::objectStreams`/`compressImages` (полная запись; nullopt — как решил режим), `subsetFonts` (`pdf_subset_fonts` шагом журнала «Subset fonts» перед записью); 3.2 — `prepare` (колбэк перед полной записью: редактор урезает свои шрифты)), `autosave.h` (`Autosave`: журнал с позиции 0, копия исходника, `index.json`, `recover`; фрагменты журнала с потоком разбирает свой `parseJournalObject` в `document.cpp` — `pdf_parse_journal_obj` MuPDF обрезает потоки > ~1 КБ, 2.4b), `revisions.h` (`versionCount`, `changeHistoryValid`, `signatures`, `signedRevisionBytes`), `props.h` (`readInfo`/`writeInfo`, `xmpPacket`, `lang`/`setLang`, `tagged`, `pageLayout` (2.2), `fonts` + `FontCache`, `advanced`), `xmp.h` (`Packet` — свой разбор и сериализатор XMP, даты PDF ↔ ISO); 2.5b — `permissions.h` (`Permissions::fromP(p, R)`
/ `of(doc)` — действующие права по таблице 22 ISO 32000-1: формы по биту 6 или 9, сборка по 4 или 11, высокое качество
только с печатью, R2 — без битов 9–12; `screenReader()`; `Document::allowed` идёт через них), `Document::isPdfX()`
(OutputIntents `/GTS_PDFX`); дальше `page.h` | 0, 1.1–1.3, 2.5b |
| `render/` | `renderer.h` (`RenderRequest{page, matrix, scissor, aa, recolor}`, `render(buffer, stride, Bgra32Premultiplied\|Rgb24, cookie)`, `renderPage`, `renderDisplayList(ctx, list, matrix, area, …)` — готовый список своим контекстом), `display_cache.h` (`DisplayList` — `shared_ptr`, свой клон контекста для отдачи в любом потоке, `contentBounds()`; `DisplayCache` — LRU 40, `invalidate(page)`, построение с cookie), `tiles.h` (`TileKey`, `pageMatrix` = scale(zoom·dpr) × rotate × translate, `pageDeviceRect`, `tileRectsFor`/`tilesIntersecting` 512 px, `makeTileKey`, превью 1/8 — `previewZoom`/`makePreviewKey`), `fit.h` (`fitZoom` по режимам, `percentToZoom`/`zoomToPercent` — 100 % = dpi/72, `clampZoom` 1–6400 %, `layoutPageSize` — 0 × 0 → Letter, > 14 400 pt — уменьшить), `bitmap.h`, `image_compare.h` (метрики `imgcmp`, `crossCompare`); 2.5b — `render_style.h` (`RenderStyle{textAa, graphicsAa,
smoothImages, thinLines, overprint, recolor}` без MuPDF, `key()` — 64 бита в поле `TileKey::style`; `Recolor` — Replace /
InvertAll, `recolorRgb` — по яркости между «текст/фон», `onlyBlack`, `lineArt`; `OverprintPreview`, `simulateOverprint`),
`recolor_device.h` (`newRecolorDevice` — обёртка над draw device: текст, векторы, маски-картинки; маски и картинки не
трогает), в `renderDisplayList(…, const RenderStyle&)` — бумага цветом фона, «инвертировать всё» по растру, симуляция
наложения — `fz_separations` только при `DisplayList::usesOverprint()` | 0, 1.1, 1.2, 2.5b |
| `text/` | `text_types.h` (значения без MuPDF: `TextOptions` — по умолчанию SEGMENT + PARAGRAPH_BREAK, `readingOrder = false` — слои поиска без сегментации, `collectVectors`/`tableHunt`/`collectStructure`/`dehyphenate`/`images`; `TextChar`/`TextLine`/`TextBlock`/`TextVector`/`TextFont`/`TextStructNode`/`TextWord`), `stext.h` (`TextPage::build(doc, page, options, cookie)` — значения в порядке чтения (struct-блоки обходятся в глубину), `text()`, `words()`, `charIndexAt`/`lineAt`/`wordAt`/`wordBounds`, `vectors()`, `structure()`, `fonts()`, `stext()` + `contextForThisThread` для функций MuPDF, `charIndexOf(fz_stext_char*)`; `TextCache` LRU 16 по {страница, флаги}), `search.h` (`SearchOptions`, `searchPage` → `SearchHit{page, firstChar, lastChar, quads}`, `SearchSession::run(pageCount, source, progress, cancel)`, `next/previous/selectFrom/counter`, `textOptions()`; 2.5 — `searchSnippet(page, hit, radius)`: строки совпадения, метки U+0002/U+0003 как у `IndexHit::snippet`, обрезка «…»), `selection.h` (своё выделение: `selectStream` + `SnapMode`, `selectRectangle`, `selectAll`, `selectRange`, `toPlainText`, `toHtml`), `index.h` (`TextIndex` на FTS5: `upsert`, `markEncrypted`, `isFresh`, `query` фразой в папке, `createSchema` — миграция 1; 2.5 — `suggest(prefix, limit)` — термины по префиксу через `fts5vocab` во временной схеме (префикс — без регистра и диакритики, как у токенизатора), `emptyFiles` — файлы без текста); дальше `reflow.h` | 1.4, 2.5, 8.3 |
| `print/` | 2.6 — `page_set.h` (`parsePageRange` — «1, 6-8, iii», «12-», «-3», «5-3», подписи с дефисом «A-1»; `resolvePages(PageSetOptions)` — все/текущая/диапазон/выделенные, чётность по физическому номеру, обратный; `formatPageRange` — выделенные миниатюры → «ii-1, 4»), `imposition.h` (чистая логика без MuPDF: `impose(sizes, pages, Paper, ImposeOptions)` → `Sheet{size, landscape, side, placements, frames, marks, tile*, label}`; `Placement{page, target, clip, rotation, scale}` в пунктах листа от левого верхнего угла бумаги, `placementMatrix`; один на листе — подогнать в область печати, остальное по центру бумаги, «Авто» по странице; n-up — `nupGrid`, лучшая ориентация листа и сетки, автоповорот, порядок, рамки; буклет — добивка до 4, лицо `[N−2k, 1+2k]`, оборот `[2+2k, N−1−2k]`, общий масштаб по наибольшей, к корешку, «только оборот» — обратный порядок; постер — наименьшее число листов, по центру, перекрытие, метки реза уголками собственной области, подписи; `Paper::a4/letter/withMargins`, поля альбомного — свои или повёрнутые; `PrintContent`), `band_renderer.h` (`buildPrintList(doc, page, PrintContent)` — `pdf_run_page_{contents,annots,widgets}_with_usage("Print")`, не PDF — `fz_run_page`; `splitBands` — ≤ 32 МиБ поровну; `deviceRect` — края round; `renderPlacement(ctx, list, placement, BandOptions{dpi, grayscale}, cookie, sink)` — полосы Rgb24/Gray8) | 2.6 |
| `edit/` | 3.1 — `content_model.h` (`ContentModel`: `build`, `streams()` — потоки страницы и форм (`ContentStream{objnum, form, parentOp, depth, bytes, ops, tail}`), `operations()`, `textObjects()`, `spans()`, `glyphs()` (`Glyph{unicode, code, codeBytes, cid, gid, w0, x, origin, quad, span, item, byteOffset}`), `fonts()`, `raw(op)`, `serialize(stream)`, `spanText`, `rebuildParagraph(block)` → `RebuildResult`, `replacements()`, `modified`, `apply(doc, operation)`, `crossCheck(doc, page, model)` → `CrossCheck`), `glyph_map.h` (`GlyphMap::load/decode`, `FontInfo{name, subtype, embedded, subset, toUnicode, widths, base14, twoByte, vertical, type3, substitute, ascender, descender, reason}`, `ReadOnly` + `readOnlyCode` — hidden, form, syntax, split, type3, vertical, rotated, fontMissing, noWidths, noUnicode, pattern, clip), `text_blocks.h` (`blocksForPage(model, BlockOptions)`, `TextBlock`, `BlockLine{spans, bbox, start, end, baseline, size, stretch}`, `StyleRun`, `Align` + `alignCode`), `empty_edit.h` (`emptyEditPage(doc, page, EmptyEditOptions)` → `EmptyEditReport`); 3.2 — `font_info.h` (`FontProbe::load/forPage` → `FontDetails`, `encode(U)` → `EncodedChar{bytes, code, cid, gid, w0}`, `hasGlyph`, `chars()`; `inspectPageFonts`; `parseFontName`, `stripSubsetTag`, `weightFromStemV`, `isSerifFamily`), `system_fonts.h` (`SystemFace{family, postscript, file, index, weight, italic, simulatedBold/Italic}`, `SystemFonts::face/byName/families`, `FontQuery`, `findSystemFont`, `metricSubstitute`, `pickFace`, `FakeSystemFonts`), `font_embed.h` (`EmbedKind` Cid/SimpleWinAnsi, `FontLibrary::font/gid/advance/embeddable`, `FontEmbedder::add/encode/width/pending/subset`, `EmbeddedFont{resource, objnum}`, `subsetFonts` → `SubsetReport`), `font_resolver.h` (`FontResolver::resolve/runs/runsInFamily/canEncode/notices`, `FontRule`, `FontChoice`, `StyleRequest`, `syntheticStyle`/`syntheticTextMatrix`/`syntheticOps`); 3.3 — `edit_types.h` (`Align`, `FontNotice`, `FallbackFonts`), `paragraph_editor.h`, `reflow.h`, `rewriter.h`, `replace.h` (см. «Правка абзаца»); `content_model.h` — `StateRefs`, `opText`, `colorOps`, `replace`; `text_blocks.h` — `underlineMarks`; 3.4 — `images.h` (`PageImage{index, op, name, matrix, pageMatrix, quad, bbox, clip, width, height, filter, group}`, `pageImages`, `imageAt`, `ImageOps::move/setRect/rotate/flip/crop/replace/add/remove/exportImage`, `ImageData`, `probeImage`), в `rewriter.h` — `EditLock`/`editLock` | 3.1–3.4 |
| `sign/` | 4.1 — `ossl.h` (OpenSSL 3.5: `Ossl<T>` с `OsslFree<T>` по типу (ASN1_INTEGER/OCTET_STRING/TIME — один `ASN1_STRING`), `OsslError` (`fromQueue(what)` — вся очередь ERR строкой, `code`/`reason`), `ossl::check`/`checkPtr`, `versionNumber`, `ensureLegacyProvider` (статический libcrypto — legacy встроен), `memBio`/`newMemBio`/`bioBytes`, `openRangeBio(path, ByteRange[])` — своё устройство BIO над `_wfopen_s`, проверка отрезков при открытии, `sha256Ranges`/`digestRanges`, `toDer(obj, i2d)`, `toHex`), `der.h` (`der::Reader` — DER и BER с неопределённой длиной, `expect/nextIf/peekTag`, `toOid`, `toInt`, `toTime`, `context(n)`), `x509_info.h` (`CertSummary`, `summarize`, `nameToString`, `commonName`, `serialHex`, `timeToIso`, `loadCertificate(s)` — DER/PEM), `pkcs12.h` (`loadPkcs12` → `SigningKey`; MAC до разбора — `BadPassword`, RC2-40 — повтор с legacy), `cms.h` (`CmsBuilder`, `cmsSignatureValue`, `cmsAddUnsignedTimestamp`, `cmsVerifyDetached(der, content, store)` — без store только целостность, со store — `CMS_CADES` и цепочка; `parseCms` → `CmsInfo{signedAttributes, unsignedAttributes, signedAttributesDer, messageDigest, signingTime, signingCertificateV2, timestampToken, certificates, signerCertIndex}`), `tsa.h` (`TsaRequest`, `TimestampInfo{genTime, serial, policy, imprint, nonce, tsaName, accuracySeconds, signer, trusted}`, `TsaError`/`TsaStatus`, `TsaClient::timestamp`/`setTrustStore`/`lastInfo`; без якорей — согласованность по сертификату токена), `chain.h` (`ChainBuilder::addUntrusted/addCrl/setCrlCheck/setPurpose/setFetcher/build`, `ChainStatus` + `chainStatusName`, `allowedForDocumentSigning`), `revocation.h` (`RevocationChecker::setOcsp/setCrl/setRetroactive/setTime/setLeewaySeconds/check/checkChain`, `RevocationResult{status, source, reason, revocationTime, thisUpdate, nextUpdate, responder, detail, ocspResponse, crl}`, `RevocationReason` по RFC 5280); 4.2 — `signer.h` (`PdfSigner` — объект MuPDF `pdf_pkcs7_signer` со счётчиком ссылок: `max_digest_size` = 16 384, `create_digest` — хэш потока /ByteRange → `contents(digest)` → DER, ошибка — 0 и `error()`; `AcroBatyaSigner(identity, digest, tsa, withoutTimestamp)` — CMS CAdES-B, B-T: метка по хэшу значения подписи; `SignRequest{identity, reason, location, contact, name, subFilter, placement{field, page, rect}, appearance, lock, timestamp, withoutTimestamp, digest, signingTime}`, `signDocument` → `SignResult{field, page, rect, visible, contentsSize, timestamped, timestamp, timestampError, save}`; `SignError` — `SignStatus` + `SaveError` + `TsaStatus`), `sign_field.h` (только core/sign: `detail::FieldJob`, `signField` — план поля (существующее пустое /Sig или новое; /Lock MuPDF у нового снимается), запись несохранённого, одна операция журнала, `SameIncrement` — `disallow_new_increments` на время патча /V (иначе MuPDF заводит новую секцию после /ByteRange), Saver::Incremental, сбой — `Saver::revert`), `doc_timestamp.h` (`TimestampSigner`, `addDocumentTimestamp` — поле `Timestamp<N>`, /Type /DocTimeStamp, /SubFilter /ETSI.RFC3161), `tsa_servers.h`, `appearance.h` (свой перенос — наибольший кегль без разрыва слов; Segoe UI контурами `fz_outline_glyph`), `identity.h` (см. «Состояние»); 4.3 — `cng_store.h` (хранилище Windows: `CertStore`, `CertContext`, `CertFilter`/`CertEntry`, `importPfx`, `filtered`,
`importIdentity`; ключ «в памяти» — только ссылкой, копия хранилища теряет дескриптор), `cng_signer.h` (`CngKey` — CACHE-флаг
обязателен для ключей PFX-импорта, `CngIdentity`, `cngCmsFromDigest`, `ecdsaP1363ToDer`), `cms_external.h` (signed-атрибуты
своим DER, `ExternalCmsBuilder`), `signer.h` — `WindowsCert`/`SigningIdentity`; 4.4 — `verifier.h` (валидатор: (1) /ByteRange из 4
чисел от 0, «дыра» — ровно hex /Contents; (2) CMS SignerInfo по шагам CMS_verify без CMS_CADES — подписант из CMS и /DSS,
хэш /ByteRange потоком, подпись над атрибутами в байтах файла, ESS v1/v2, хэш OID подписи = digestAlgorithm; SubFilter
ETSI.CAdES.detached/PBAD.PAdES, adbe.pkcs7.detached, adbe.pkcs7.sha1, ETSI.RFC3161; (3) цепочка до якорей на момент метки
или /M; (4) отзыв — /DSS и adbe-revocationInfoArchival, затем сеть только для сертификатов без данных, «LTV»; (5) метки;
(6) изменения — `changes.h`), `trust_store.h`, `docmdp.h`, `changes.h` (роли объектов по достижимости из каталога,
разбор каталога/страницы/`/Annots`/`/AcroForm`/поля, подписанная ревизия — временный документ той же семьи); 4.5 —
`ltv.h` (сбор цепочек подписанта и службы времени, отзыв и респонденты OCSP — `Collector`; запись /DSS — `DssArray`,
потоки без повторов по DER, /VRI по опции; «LTV включено»; продление B-LTA), `warnings_scan.h` (`Scanner` — каталог,
дерево имён, страницы, комментарии, поля, закладки, цепочки /Next), `eutl.h` (логика EUTL без XML), `aatl.h` (разбор
SecuritySettings.xml по факту), в `docmdp.h` — запись DocMDP и правила, в `signer.h` — уровни и сертификация, в
`trust_store.h` — импорт/экспорт и корни Windows по назначениям; 4.6 — `sig_fields.h` (`SignatureField{name, page, rect, visible, isSigned, readOnly, docTimestamp}`, `signatureFields(doc)` — все /FT /Sig в порядке /AcroForm /Fields, `emptyFieldAt(fields, page, point)`, `ClearBlock` + `clearBlock(doc, field)` — только последняя и не заблокированная, `clearSignature` — своя очистка в операции «Clear signature» (`pdf_clear_signature` MuPDF 1.28 оставляет операцию журнала открытой): `pdf_signature_appearance_unsigned`, /V снят у поля и виджета, Locked снят, у сертифицирующей — /Perms /DocMDP), в `x509_info.h` — `certDetails(der)` → `CertDetails` (назначение ключа, EKU, политики, OCSP/CRL, расширения текстом OpenSSL, SHA-1/SHA-256, `selfSigned`), в `verifier.h` — `VerificationTime` (SigningTime/SecureTime/Current) и `VerifyOptions::time` | 4.1–4.6 |
| `annot/` | `annots.h` (2.2, минимум: `AnnotRef{page, objnum}`, `AnnotType` — 28 подтипов в порядке `pdf_annot_type`, `AnnotInfo{ref, type, subtype, rect, flags}`; `listAnnots` — весь `/Annots`, с Link/Popup/Widget, которых нет в списке `pdf_annot` MuPDF; рамки — координаты страницы MuPDF (`pdf_bound_annot`, NoZoom/NoRotate, `/Rotate`); `findAnnot`, `annotAt` — верхняя, без Popup и скрытых; `setRect` — `/Rect` без пересоздания `/AP`; `remove` — `pdf_delete_annot` (с его Popup) или из `/Annots`; правки — `Journal::Operation`; 28 типов, сеттеры и JSON — 5.1), дальше `appearance.h`, `popups.h`, `fdf.h`, `xfdf.h`, `stamps.h`, `measure.h`, `summary.h`, `fillsign.h` | 2.2, 5.1–5.5 |
| `forms/` | `fields.h`, `field_factory.h`, `js_bridge.h`, `data_io.h`, `submit.h`, `detect.h`, `tab_order.h` | 6.1–6.4 |
| `pages/` | 2.4 — `labels.h` (чтение `/PageLabels`: `pageLabels`, `pageLabel`, `parsePageInput` — «iv», «3» (логический, иначе физический), «(3)»); 2.5b — `boxes.h` (`pageBoxes(doc, page)` — Media/Crop/Bleed/Trim/Art в координатах `fz_bound_page_box`, `has*` — задана с наследованием); 7.1 — `organizer.h`, `graft.h`, `split.h`, запись в `labels.h` и `boxes.h`, `insert.h` | 2.4, 2.5b, 7.1 |
| `create/` | `to_pdf.h` (2.3: `renderToPdf`/`writeAsPdf` — документ «только просмотр» (XPS, EPUB, картинки, TXT) → PDF через `fz_document_writer` «pdf», страницы как есть, отмена между страницами), `text_export.h` (`exportText(doc, TextExportOptions{format, encoding, bom, firstPage, lastPage}, cancelled)`: TXT UTF-8/UTF-16/CP1251 с CRLF и «\f» между страницами, HTML/XHTML/XML через `fz_print_stext_page_as_*`, RTF), `rtf_writer.h` (`RtfWriter`, `rtfText`, `rtfFontFamily`) — 1.4; дальше `dispatcher.h`, `image_to_pdf.h`, `pdf_to_images.h`, `tiff_writer.h`, `combine.h`, `html_to_pdf.h`, `presets.h` | 1.4, 7.2 |
| `decor/` | `header_footer.h`, `watermark.h`, `background.h`, `artifact.h` | 7.4 |
| `links/` | `links.h` (2.2b: `pageLinks(doc, page)` → `LinkInfo{page, objnum, rect, LinkAction}` — рамки и адреса `fz_load_links`, вид действия (`LinkKind` GoTo/GoToR/Uri/Named/Launch/Unsupported) из словаря `/Link`, ссылка MuPDF сопоставляется по рамке; назначение — `fz_resolve_link_dest`: `page`, `DestFit`, `point` (NaN — нет компоненты), `zoom` (1 = 100 %); `linkAt(list, point, tolerance)`; 2.4 — `actionFromUri(doc, uri)`), 2.4 — `outlines.h` (`outline::children(doc, path)` — уровень
закладок итератором MuPDF без всего дерева: заголовок, действие, /F, /C, `open`, `hasChildren`, путь; у картинок итератора нет —
пусто), `destinations.h` (`dests::list` — `/Names /Dests` и `/Dests` каталога; назначение — сами, `pdf_parse_link_dest`: MuPDF при
`/Dests` каталога в дерево не смотрит), `attachments.h` (`attachments::list/contents` — дерево `/EmbeddedFiles` и `/FileAttachment`
всех страниц; `isBlocked` — чёрный список эталона 7.3, `safeFileName` — имя для диска), `layers.h` (`layers::list/toggle/setOn/reset/
stateHash` — строки конфигурации `/D`; состояние просмотра, не правка), `initial_view.h` (`readInitialView`: `/PageMode`,
`/PageLayout`, `/OpenAction` → `LinkAction`, `/ViewerPreferences`, `/Info /Title`; 2.5 — `writeInitialView` (операция «Initial view»: false и пусто удаляют ключ, чужие ключи `/ViewerPreferences` остаются; PrintScaling, Duplex — 7.5), `hasViewerPreferences`, `baseUrl`/`setBaseUrl` (`/URI /Base`, операция «Base URL»)); дальше `links_auto.h` | 2.2b, 2.4, 2.5, 7.5 |
| `crypt/` | `password.h`, `permissions_write.h`, `policies.h`, `strength.h` | 7.6 |
| `ocr/` | 3.4 — `is_scan.h` (`checkScan` → `ScanCheck{scan, hasText, images, coverage}`: своё устройство MuPDF — текст и рамки картинок; скан — текста нет и картинка ≥ 90 % страницы); 2.4b — `tessdata.h` (`findTessdata` — `{app}\tessdata` или `TESSDATA_PREFIX`, `tessdataLanguages`, `probeTessdata` — настоящая загрузка языка через `ocr_init`/`ocr_fin` MuPDF, путь 8.3; `--self-check`); дальше `engine.h` (`TessBaseAPI`), `text_layer.h`, `orientation.h`, `enhance.h`, `suspects.h`, `scan_to_pdf.h` | 2.4b, 8.1–8.2 |
| `a11y/` | `autotag.h`, `struct_tree.h`, `reading_order.h`, `lang.h` | 8.3–8.4 |
| `optimize/` | `qpdf_tool.h` (`QpdfTool`: поиск qpdf.exe — `OTTISK_TOOLS`, рядом с exe, `C:\Tools\qpdf\bin`; `check`, `linearize`, `checkLinearization`, `json`; процесс без окна), дальше `optimizer.h` | 1.3, 9.3 |
| `util/` | `paths.h` (единственная точка UTF-8/wchar и `toShortPath()`), `geometry.h` (`RectF`, `IRect`, `Matrix` как у MuPDF), `features.h` (`OT_FEATURE`), `hash.h` (SHA-256 через OpenSSL), `utf8.h` (`utf8::append/encode/decode` — char32_t ↔ UTF-8), 4.1 — `http_fetcher.h` (`net::HttpFetcher` — единая точка сети без Qt: `Request{method, url, contentType, body, headers, timeoutMs 15000, sameHostRedirectsOnly, maxResponseBytes, purpose}`, `Response{status, httpCode, contentType, body, error, finalUrl, ok()}`, `Status`, `Purpose::Signatures/General`, `mediaType`; `FakeHttpFetcher` — `add`/`addBody`/`setHandler`, `requests()`), в `geometry.h` с 1.4 — `PointF`, `QuadF` (`bounds`, `contains`), `rectDistance`, `rectUnion`, `RectF::none()`; дальше `log.h`, `fs.h`, `ids.h` | 0, 1.1, 1.3, 1.4 и далее |

### `src/services/` — Qt без Widgets (страж: ни одного `QtWidgets`)

`document_session.h` (поток документа: очередь задач с приоритетами, `post`/`edit`/`postSync` → `QFuture`, `cancelAll`,
`displayList(page)`, сигналы `pageChanged`/`structureChanged` (2.2: `touchAllPages` — все страницы другие, до
`documentChanged`)/`documentChanged`/`taskFailed`/`undoStateChanged`/`saved`, `OpenSummary` с размерами раскладки (повреждённая
страница — Letter, не ошибка открытия) и `/PageLayout`; `edit` — в операции журнала, `undo`/`redo`/`undoState`, `save`/`saveAs` через `Saver`,
`recoverAsync`; QFuture завершается после revision, раскладки и истории), `task_context.h` (что видит задача:
`document()`, `context()`, `cookie()`, `page()` → `PageHandle`, `displayList()`, `touchPage()`, `touchAllPages()`,
`markHeavyEdit()`; с MuPDF — ui его не включает), `autosave_runner.h` (`AutosaveRunner`: таймер 1–30 мин, `watch`,
`tick`, `saveNow`, `discard`, `recoverable`, `recover`), `document_registry.h` (сессии по id вкладки, один файл — одна вкладка),
`render_pool.h` (`RenderPool`: `QThreadPool` idealThreadCount − 2, `submit(TileRequest)` → `tileReady`/колбэк, `cancel`,
`cancelDocument`, `retarget`, `setPriority`), `tile_cache.h` (`TileCache`: `QCache` тайлов 7/8 и превью 1/8 лимита,
384 МБ, `limitFromMegabytes`, `shrink`), `memory_guard.h` (фон и простой 60 с → `fz_shrink_store` 50 % и `lowMemory`; с Р1 живёт в `MainWindow` вместе с её `TileCache` — `MainWindow::memoryGuard()`),
`app_info.h` (версия, пути для `--self-check`), `state_db.h` (`StateDb::shared()` — одно подключение
`state.sqlite` в каталоге данных, WAL, миграции по `PRAGMA user_version` — шаги в `state_db.cpp`, испорченный файл →
`.bad`; `handle()` только под `mutex()`), `folder_search.h` (`FolderSearch::run/start` — обход, извлечение в своём пуле,
индекс, результаты по файлам, `encrypted`/`failed`, сигналы `progress`/`fileFound`/`finished`), `clipboard_io.h`
(`ClipboardIo::textMime` — text/plain + text/html, `imageMime` — PNG, `put` — нужен QGuiApplication, `snapshot` —
область страницы при dpi), в `DocumentSession` с 1.4 — `textPage(page, options)` и `exportText(path, options)`,
`annot_ops.h` (2.2: `annot::list`/`at` — задачи, `setRect`/`remove` — `edit` с шагами «Set annotation rectangle» /
«Delete annotation» и `touchPage`; ui зовёт их, а не `TaskContext`; 2.2b — `setRects` — несколько рамок одним шагом),
`link_ops.h` (2.2b: `links::list(session, page)` — задача над `core/links/links.h`), в `ClipboardIo` с 2.2b —
`textMime(vector<PageSelection>)` — выделение нескольких страниц (`toHtmlFragment` + `htmlDocument` из `selection.h`), `i18n.h` (2.1: `install(язык)` — `:/i18n/ottisk_<язык>.qm` с qtbase, прежний переводчик снимается, `QLocale::setDefault`,
окна получают `LanguageChange`; `resolve` — ru|en|system; `fromSettings` — ключ `language/ui` в `settings.ini`); дальше —
2.3 — `commands.h` (`CommandRegistry`: `Command{id, title, shortcuts, icon, run, when, enabled, menu, separatorBefore,
singleKey, checkable, checked, stage}`; контексты `when`: пусто — окно, `"view"` — фокус в документе, режим
(`setContextActive`) — команда живёт, пока режим включён, и забирает клавишу у команды окна; однобуквенные —
`hotkeys/singleKeyAccelerators`, переназначения — `hotkeys/<id>` (`loadSettings`); `effectiveShortcuts`, `conflicts`,
`trigger`, `toJson`; объект окна — `MainWindow::commands()`), `single_instance.h` (`SingleInstance`: имя
`AcroBatya-<SID>-<OTTISK_INSTANCE|main>`, `sendToRunning` — второй экземпляр, `listen` + `requestReceived`, строка JSON),
`session_store.h` (`session::save/load` — вкладки прошлого сеанса, `session_tabs`, ключ `general/restoreSession`),
`recent.h` (`RecentFiles::shared()`: `touch`, `recent` — 50, закреплённые первыми, `favorites`, `setPinned`, `setFavorite`,
`remove`, `saveView`/`lastView` — последний вид; сигнал `changed`), `thumbnails.h` (`ThumbnailCache::shared()`: `get` —
готовая или пустая + рендер первой страницы 160 px в своём пуле, `ready(path)`, кеш `thumbs\<sha1>.png`, `renders()`),
`convert_ops.h` (`convert::saveAsPdf` — задача над `core/create/to_pdf.h`), в `app_info.h` — `settingsPath()`; 2.4b —
`settings_keys.h` (все ключи settings.ini с умолчаниями: `BoolKey`/`IntKey` с пределами/`StringKey` с `choices`/`ListKey`,
группы по категориям Установок и состоянию окна — `navpane/`, `window/`, `fullScreen/`; классовые константы 2.3–2.4
(`NavPane::kOpenKey`…) — их псевдонимы), `settings.h` (`Settings`: `get/set` типизированно — чужое значение → умолчание,
`raw()` для групп `hotkeys/<id>`, `installDefaultFormat` — INI по умолчанию и в каталоге данных, `migrate` — `meta/version`;
Qt пишет группу `general` как `[%General]`), `script_runner.h` (`ScriptRunner::registerAction(id, schema, fn)`, `run`/`runFile`
— шаги проверяются до выполнения, `ScriptContext` — `path`/`expand`/`shotsDir`, `ScriptError`, `ScriptResult::describe`),
`props_ops.h` (`props::read`, `props::setProperty(key, value)` — шаг «Set document property»), `open_probe.h` (`probeOpen` —
открыть → стр. 1 в 32 × 32 → закрыть: открыт/отказ/сбой), в `app_info.h` — `selfCheck` целиком, `pluginsDirs`/
`missingPlugins` (по файлам в путях поиска Qt), `associatedPdfProgram`, в `DocumentSession` — `saveCopy`; дальше —
`office_converter.h` (+`FakeOfficeBackend`),
`word_converter.h`, `scan/{iscansource,wia,fake}.h`, `tts.h` (+Fake), `mail.h` (+Fake),
`shell_register.h`, `help.h`,
`ocr_service.h`. С 2.4: `shell_launcher.h` (`ShellLauncher::instance()` — `openUrl`,
`openFile`, `openSettings`, `showInExplorer`; без живого рабочего стола (offscreen, QCoreApplication) — отказ; `FakeShellLauncher`
записывает вызовы, `ScopedShellLauncher` подменяет на время; ui-тесты и `--shots` ставят фейк сами), `nav_ops.h` (`nav::outline`,
`destinations`, `attachments`, `saveAttachment`, `layers`, `toggleLayer`, `resetLayers`, `transitions` — задачи над core),
`background_tasks.h` (`BackgroundTasks::shared()`: `start(title, cancel)`, `setProgress`, `finish`, `cancel`, сигнал `changed` —
строка состояния), в `DocumentSession` — `layersHash()` + сигнал `layersChanged` (`TaskContext::setLayersState(hash)`: display
lists и текст сброшены, revision не растёт), в `OpenSummary` — `pageLabels`, `initial` (`InitialView`). С 2.5b:
`protected_view.h` (`zoneId`/`writeZoneIdentifier` — поток `Zone.Identifier`, `tempFolders` + `setTempFoldersForTesting`
(ui_main ставит пустой список — папки тестов в `%TEMP%`), `fileKey` — SHA-256 первых 64 КиБ + размер, `trust`/`isTrusted`
— `trusted_files` scope `protectedView`, `check(path, attachment)` → `Origin` по `trust/protectedView` и
`trust/trustedFolders`), `link_policy.h` (`LinkTrust` — сайты `link_hosts`, `trust/internetAccess`; `decideLink` →
`LinkDecision`; `remoteFilePath` для `/GoToR`), `sql.h` (`SqlStatement` — запросы `state.sqlite`), `box_ops.h`
(`boxes::read`), в `OpenSummary` — `permissions`, `pdfx`; в `commands.h` — `Command::blocked` + `blockedReason`; в
`TileRequest` — `style` (`RenderStyle`).
2.6: `print_job.h` (`PrintDevice` — бумага/dpi/листы/рисование в пикселях устройства от угла бумаги:
`makePrinterDevice(QPrinter&)` — HighResolution, `setFullPage(true)`, поля `minimumMargins` обеих ориентаций, ориентация
листа перед `newPage()`, копии и подбор драйвера, дуплекс, лоток; `makePdfDevice` — QPdfWriter (суррогат PdfFormat в
тестах); `makeGdiDevice` (`print_job_gdi.cpp`: `CreateDCW(L"WINSPOOL")` + `StartDocW` с `lpszOutput` — «Microsoft Print to
PDF» без окна, `StretchDIBits` снизу вверх); `FakePrintDevice` — листы `QImage`, подписи шрифтом только при
QGuiApplication; `PrintDevices::printers/defaultPrinter/supportsDuplex/paperFor/open` — подмена `ScopedPrintDevices`
(тесты; ui_main и `--shots` ставят `demoPrinters()`/`demoPrinterFactory()`), `OTTISK_FAKE_PRINTER=<каталог>` — PDF и
`jobs.jsonl` (процессы exe); `PrintSettings`, `makePrintPlan` → `PrintPlan{status, pages, sizes, sheets, duplex}` (буклет
«обе стороны» — по короткой стороне), `printDpi` (потолок `print/quality`, «как изображение», без высокого качества ≤ 150),
`PrintJob::run(device, progress)` — display lists задачами сессии, растр и рисование в вызывающем потоке (не поток
документа), копии с подбором и без, пустой оборот при дуплексе, `cancel()` — cookie + abort; `PrintJob::preview(paper,
sheet, dpi)` — тот же растр в QImage; `printFileSilently(path, printer)` — `/p`, `/pt`), в `settings_keys.h` — `print/`
(принтер, качество, масштаб, папка и шаблон «Печать в PDF», «спрашивать»), в `single_instance.h` — `Request::print/printer`,
в `app_info.cpp` — `--self-check` «print verbs» (ProgID `shell\print`, `shell\printto`).
4.6: `crypto_worker.h` (`CryptoWorker::shared()` — пул из двух потоков: `verify(session, VerifyRequest)` — своя копия записанного файла (зашифрованный и без файла — задачей сессии; OCSP/CRL/AIA не держат поток документа), `verifyFile(path, request)`, `waitForDone`; `VerifyRequest{trust, network, revocation, acceptExpiredTimestamps, time}` — `fromSettings(Settings)` (снимок `TrustService`, ключи `signatures/…`, `trust/network`), `options()`), `trust_service.h` (`TrustService::shared()` — один `TrustStore` каталога `<данные>\trust` на процесс: `snapshot()` — неизменяемый снимок для проверки и LTV, `edit(fn)` — правка копии под замком → anchors.json → новый снимок → сигнал `changed` (вкладки проверяются заново), `reload`, `dir`; сменился `OTTISK_DATA_DIR` — перечитывается), `sign_ops.h` (`signing::fields` → `SignatureField`, `warnings`, `certifyBlock`, `docMdp`, `clearBlock`/`clearSignature` — шаг «Clear signature», записать — `save` инкрементом, `signedVersion(session, field)` — [0, конец /ByteRange) в `%TEMP%\AcroBatya\<имя>.signed-<N>.pdf`, `ltvDecision(session, trust, LtvAutoAdd)` → `LtvDecision`), `sign_settings.h` (`NamedAppearance`, `loadAppearances`/`saveAppearances` — JSON `signatures/appearances`, `appearanceToJson`/`appearanceFromJson`; ярлыки строк вида не хранятся); в `settings_keys.h` — `kSignShowReason`, `kSignShowLocation`, `kSignReasons`, `kSignAppearances`, `kSignAppearance`, `kSignReviewWarnings` (never|certify|always), `kVerifyOnOpen`, `kVerifyRevocation` (required|bestEffort|never), `kVerifyExpiredTimestamps`, `kVerifyPromptTrust`, `kVerifyTime` (signing|secure|current).
4.5: `DocumentSession::addValidationInfo(trust, vri, target)` / `extendTimestamp(tsa, trust, target)` — /DSS и продление метки (сеть — `httpFetcher()`); `trust_lists.h` (`parseLotl`/`parseTrustedList` — QXmlStreamReader в модели `core/sign/eutl.h`, `lotlPins()` — `:/trust/lotl_pins.json` (`qt_add_resources` у `ottisk_services`), `HttpTrustListSource` (Purpose::Signatures, TL до 64 МиБ), `downloadTrustLists(fetcher, eutl, aatl, now)` → `TrustListUpdate{eutl, aatl, needsConsent()}`, `saveTrustListCache`/`loadTrustListCache` — `<данные>\trust\{eutl,aatl}.json`, `installTrustLists` → `TrustStore::replaceSource` + anchors.json), `trust_updater.h` (`TrustUpdater(settings, fetcher, trustDir)`: `check(force)` → `Outcome` NotDue/Disabled/Failed/Pending/Installed, попытка засчитывается всегда, `install`/`discard`, сигналы `updateAvailable(eutl, aatl, needsConsent)`/`installed`); в `settings_keys.h` — `kAutoAddLtv`, `kLtvVri`, `kTrustEutl`, `kTrustAatl`, `kTrustListsAsk`, `kTrustListsCheckedAt`.
4.2: `DocumentSession::sign(SignRequest, target)` / `timestampDocument(tsa, target)` — задачи потока документа с записью и переоткрытием (`afterSave(report, reloaded)`), ошибка — `sign::SignError` в QFuture; `tsa_settings.h` (`loadTsaServers`/`saveTsaServers` — ключ `signatures/tsaServers`, `timestampFallback`, `makeTsaClient`/`defaultTsaClient` поверх `httpFetcher()`), `dpapi.h` (`dpapi::protect/unprotect` с энтропией программы, `DpapiProtector`), `identities.h` (`Identities::shared()` — `IdentityStore` над `StateDb::shared()` и `<данные>\ids`, `with(fn)` под замком базы); 4.3: `cert_picker.h` (`CertPicker::available()` — есть QGuiApplication не offscreen/minimal, `pick(hwnd, заголовок, подсказка)` → `PickedCertificate{thumbprint, name, subject, issuer, notAfter, der, smartCard}` — `CryptUIDlgSelectCertificateFromStore` над `cng::filtered(«Личное»)`); в `settings_keys.h` — `kTsaServers`, `kTimestampFallback`, `kTimestampSignatures`, `kSignCompatibility`; state.sqlite — миграция 5 (`identities`).
4.1: `http_fetcher.h` (`QtHttpFetcher` — `net::HttpFetcher` на QNAM синхронно в потоке вызова: свой QNAM и `QEventLoop`, общий срок `timeoutMs`, `UserVerifiedRedirectPolicy` — только тот же хост и схема не слабее (`sameHostRedirectsOnly`), `maxResponseBytes`, журнал spdlog «net: метод адрес -> код (байты, мс)» без тела и userinfo; `NetworkAccess` Allowed/SignaturesOnly/Off — `networkAccess(Settings)` по `trust/network`, без аргумента — из настроек на каждый запрос; `httpFetcher()` процесса, `setHttpFetcher`, `ScopedHttpFetcher`); в `settings_keys.h` — `kNetworkAccess`.
3.4: `edit_ops.h` (`editing::pageContent` → `PageContent{page, blocks, images}`, `Block{bbox, reason, editor}`;
`newParagraph` — пустой абзац «Добавить текст»; `moveImage/setImageRect/rotateImage/flipImage/cropImage/replaceImage/
addImage/removeImage/exportImage` — шаги «Move image»…; `lock` — подпись или `/Perms`), `ocr_ops.h` (`ocr::scanCheck`),
в `DocumentSession` — `findReplace` (предпросмотр), `replaceHit` (шаг «Replace text»), `replaceAll` (один шаг
«Replace all» на команду, прогресс и отмена).
3.2: `system_fonts_dwrite.h` (`DWriteSystemFonts` — `edit::SystemFonts` на IDWriteFactory3: семейство, PostScript- и полное имя грани, путь и индекс файла, симуляции; общая фабрика без CoInitialize, вызовы под mutex из любого потока; `systemFonts()` процесса; `editFallbackFonts(Settings)` — ключ `editing/fallbackFont`).
2.5: `search_ops.h` (`search::run` — поиск по документу задачей: прогресс — новые совпадения колбэком из потока документа,
отмена — `Control`, `Result::noText()` — скан без OCR; `bookmarks`, `attachments` — PDF-вложения из памяти, `runAll` —
«Расширенный поиск» по документу одной задачей, `matches`), в `folder_search.h` — `FolderSearch::suggest` (подсказки по
индексу) и `FolderSearchResult::noText`; в `props_ops.h` — `readSheet` (`Sheet`: всё для «Свойств», кроме шрифтов и
проверки), `fonts`, `check` (`pdf_check_document`; ремонт сбрасывает историю), `apply(Changes)` — один шаг «Document
properties», `xmpProperties`, `Rights`; `save_ops.h` (`saving::decide` — `Saver::decide` задачей High, до записи); в
`DocumentRegistry` — `recover(autosaveDir, entry, password)`.

### `src/ui/` — Qt Widgets (страж: ни одного `mupdf/`)

| Каталог | Что | Сессия |
|---|---|---|
| `app/` | `main.cpp` (QApplication, `registerBuiltinTools`, тема `Theme::apply` до окна, язык `i18n::install`, `--showcase`, `--commands-list` (JSON реестра команд для стража), один экземпляр — `SingleInstance::sendToRunning`/`listen`, сеанс по настройке; 2.5 — `MainWindow::offerRecovery` после показа окна, кроме `--smoke`), `cli.{h,cpp}` (аргументы эталона 3.18: `--theme system\|light\|dark\|contrast`, `--scale`, `--lang`, `--shots`, `--shots-list`, 2.3 — `--page N`, `--log`, `--data-dir`, `--commands-list`; 2.4b — `--script`, `--open-all`, `isConsoleMode` (режимы на QCoreApplication без QPA), `runSelfCheck`, `runOpenAll`/`openAll`, `runRegisterNotify` — заглушка до 9.1; 2.6 — `/p файл`, `/pt файл принтер [драйвер порт]` → `runPrint` на QApplication без окна: запущенному экземпляру — `SingleInstance::Request::print`, иначе `printFileSilently`, код 0/1), `script_actions.{h,cpp}` (2.4b: `registerBaseActions` — doc.*, view.*, `command.run`, `shot`, `wait`; `runScript` — окно 1440 × 900, «Не сохранять»; помощники `script::await/settle/document/tilesReady`; 4.6 — в конце `registerBaseActions` — `registerSignScriptActions`); 4.6 — `main.cpp` зовёт `MainWindow::checkTrustListsLater()` (не `--smoke`, не тесты) | 0, 2.1, 2.3, 2.4b, 4.6 |
| `shell/` | 2.3: `main_window.{h,cpp}` (`MainWindow`: `openFile(path, page)`/`openFiles`, `closeDocument` с `CloseConfirm`, `showHome`/`showDocument`, `currentDocument`/`document(i)`, `tabs()`, `globalBar()`, `toolPanel()`, `commands()`, `registry()`, `sessionState`/`restoreSession`, `handleRequest` (второй экземпляр), `notifyLater(что, этап)` — тост заглушки, `setSingleKeyAccelerators`, `setPanelsHidden` (F8); ряд «`ToolPanel`; 2.5b — `link_flow` (`LinkFlow::follow(view, action)` — `decideLink`, диалог, `ShellLauncher::openUrl`, `/GoToR` вкладкой; `setConfirm` для тестов; окно подключает к `PageView::linkActivated`), в `DocumentView` — `setProtectedView(Origin)`/`isProtectedView`, `permissions()`, `canSave`/`canAnnotate`, `setViewStyle`, `isPdfX`, раскладка и масштаб по умолчанию из `pageDisplay/`; в `MainWindow` — `applyProtectedView`/`enableAllFeatures` (жёлтая полоса), `linkFlow()`, `applyViewStyle`, `setRecolorMode` (команды `view.nightMode`, `view.highContrast`); «Только чтение» в строке состояния и для защищённого просмотра | `QSplitter` (центр — `QStackedWidget` Главная / документы; правая навигация 2.4)», строка меню скрыта (F9); вкладки удаляются в деструкторе до пула и кеша), `menus.cpp` (`registerCommands` — команды окна в реестре, `registerToolCommands` — «Инструменты» из `ToolRegistry`, `rebuildMenus` — меню из реестра, `updateActions`), `global_bar` (48), `document_tabs` (36, ширина 140–220, точка «несохранено», средняя кнопка, ◀ ▶), `document_view` (`DocumentView`: `session()`, `view()`, `isModified`, `isViewOnly`, `addMessage`, `setFloatingBar`, `setInitialPage`/`setInitialView`/`setInitialAction` (2.4 — `/OpenAction`), `currentView`, `save`, 2.4 — `messageStack()`, `pool()`),, 2.5b |
2.4 — `nav_pane` (`NavPane` — полоса 44: `showPane/closePanel/togglePanel` (F4), `setShown`, `setFieldsVisible`,
`setDestinationsButton`, `replacePage` (сессия-владелец ставит настоящую страницу вместо заглушки), `railMenu`; `NavPanel` — панель
280 в сплиттере окна; ключи `navpane/open|current|width|destinations`), `status_bar` (`StatusBar` 32: поле страницы
`pageDisplay/useLogicalPageNumbers`, ZoomBox, раскладка, поворот, значки, фоновая задача; `focusPageField` — Ctrl+Shift+N),
`message_bar` (`MessageStack`: `showMessage(kind, text)` — полоса на вид по важности, `add(tone, text)` — своя, `remove`, `bars`),
`read_mode` (`ReadModeBar` и `MainWindow::setReadMode/applyPanels/placeReadBar`; Esc — фильтр приложения, контекст команд
`readMode`), `full_screen` (`FullScreenWindow(session, pool, cache, page)`: `start(screen)`, `next/previous/goTo`, переходы
поверх живого вида, `FullScreenSettings::load/save` — группа `fullScreen/`); в `MainWindow` с 2.4 — `navPane()`, `status()`,
`setReadMode`, `enterFullScreen`, `applyInitialView` (`/PageMode` → панель, `/FullScreen` → полоса с кнопкой, DisplayDocTitle →
заголовок), `window/panelsHidden` (F8) и `tools/panelCollapsed` (Shift+F4) в ini, `showInExplorer` через `ShellLauncher`;
`tool_panel` (320/48: «Все инструменты», панель инструмента, полоса иконок; `toolRequested`), `quick_bar` (`QuickBar` на `FloatingBar`, `attach(PageView*)`, `quickbar/order`, `setSingleKeyHints`), `home/` (`home_page` — `HomePage(RecentFiles&, ThumbnailCache&)`: разделы, поиск, сетка/список, «Создать PDF ▾», `scansFolder()`; `recent_model` (роли `home::PathRole`…), `files_model`, `thumb_delegate`, `brief_pane`); 2.4b — `preferences/` (`PreferencesDialog` — `showCategory`, `page(id)`, `applyAll`, сигнал `applied`, `open()` без своего цикла; `PreferencesPage` — `categoryId`, `title`, `stage`, `load`/`apply`, `fillData`/`selectData`; живые `GeneralPage`, `PageDisplayPage`, `DocumentsPage`, остальные — `StubPreferencesPage` с версией: сессия-владелец заменяет строку `addPage` своей страницей), `command_palette` (`CommandPalette(CommandRegistry&)`: `setFilter`, `visibleIds`, `runCurrent`, `matches` — ранги), в `MainWindow` — `showPreferences(категория)`, `applyPreferences` (тема, крупные элементы, язык, однобуквенные, строка состояния), `showCommandPalette`, `openFile(path, page, password)`; 2.5 — `save_flow` (`SaveFlow`: `save` — `saving::decide` до записи: нет файла → `saveAs`, `WouldBreakSignatures` → предупреждение и `saveAs(view, true)` — копия через `saveCopy`, иначе запись; `saveAs` — `SaveAsDialog`, свой файл — `save`, другой — `saveAs`; `errorText(SaveError)` — тост; `rewriteNotice(view, операция)` — полоса нового документа после операции с перезаписью; `setSignaturePrompt`/`setSaveAsPrompt` — ответы в тестах), в `MainWindow` — `saveFlow()`, `autosave()` (`AutosaveRunner` окна, настройки из Установок; `closeDocument`/`closeEvent` удаляют запись), `setPasswordPrompt`, `hasRememberedPassword`, `offerRecovery`/`recoverDialog`/`recoverDocuments`, `addDocumentView` (полосы «восстановлен» и PDF/A, автосохранение), `showSearchWindow`/`searchWindow`, `showProperties(tab)`/`propertiesDialog`; в `DocumentView` — `findBar()`, `setReadOnly`/`isReadOnly`/`canEdit` + `readOnlyChanged` (команды правки — по `canEdit`); 4.6 — `signature_status` (`SignatureStatus` — `DocumentView::signatures()`, `signatures_.reset()` первым в деструкторе `DocumentView`: `verify` (при открытии по `signatures/verifyOnOpen`, после `DocumentSession::saved` и `TrustService::changed`; устаревший итог — по поколению), `isVerifying`, `hasResult`/`verification`, `signature(field)`, `fields()`, `error`, `bar()` — синяя/жёлтая/красная полоса, `badges()` — слой значков `SignatureBadges` z 45; сигналы `started`, `verified`, `paneRequested`, `trustRequested(field)`), в `MainWindow` — `SignaturesPane` вместо заглушки, «Доверять подписанту…» → `CertificateViewer` на корне цепочки, вкладка «Доверие», `openSignedVersion(source, path, label)` (вкладка «только чтение», временный файл в `temporary_` уходит с вкладкой), `checkTrustListsLater(delayMs = 5000)` (`TrustUpdater` в фоне, тост «Установить»), вкладка следует за файлом по `DocumentSession::saved`; в `StatusBar` — чип «Подписано» тоном итога проверки; `preferences/` — `signatures_page` (`SignaturesPage`: категория `signatures`, вкладки `tabIds()` — creation, verification, identities, trusted, tsa, `showTab(id)`; внешние виды — `newAppearance`/`editAppearance`/`removeAppearance`/`setDefaultAppearance` через `AppearanceDialog`), `identities_widget` (`IdentitiesWidget`: `addPfx`, `setDefaultSelected`, `exportSelected` .cer/.p7c/.fdf, `changePassword`, `setTimeoutSelected`, `forgetPasswordSelected`, `removeSelected(deleteFile)`), `trusted_widget` (`TrustedWidget`: якоря `TrustService` с фильтром по источнику, `importFile`, `exportSelected`/`exportAll`, `editSelected` — `TrustDialog`, `removeSelected`, `importWindows(root, ca)`, `updateNow` — `apply` + `sync`, затем `TrustUpdater` в фоне, сигнал `updateFinished`), `tsa_widget` (`TsaWidget`: `addServer`/`updateSelected`/`removeSelected`/`setDefaultSelected`, `checkSelected` — тестовая метка в фоне, `checkFinished`); удостоверения, якоря и корни Windows — сразу, остальное — по «ОК»; ответы тестов — `setFileChooser`/`setPasswordPrompt`/`setTrustPrompt`/`setConfirm`/`setServerPrompt` | 0, 2.3–2.5, 4.6, 9.1 |
| `theme/`; 2.5b — в `tokens.h` `kNightReading`, `kHighContrastReading` (пары «текст/фон» замены цветов), `kPageBoxColors` (рамки областей) | `tokens.h` (`constexpr` палитры `kLightPalette`/`kDarkPalette`/`kHighContrastPalette` — имена таблицы 6.4 + `onAccent`, `form`, `shadow`; `kAnnotationColors`, `kPaperColor` (бумага — цвет документа, 2.2); `Metrics` и `kMetricSizes` (2.2: `pageGap` 8,, 2.5b |
`toolWindowWidth/Height` 280 × 220, `pageBorder` 1 — вне `kMetricSizes`, «Крупные элементы» её не меняют); `Tokens::base/scaled/palette`), `theme.h` (`ThemeChoice`, `Theme::apply(app, choice, scale)`, `themeChanged`, `Theme::colors()/sizes()`, `mix`, `softBackground`, `rowColors`, `componentColors`), `palette.cpp` (QPalette), `qss.cpp` (база + таблица правил компонентов → QSS и пары контраста), `fonts.cpp` (`uiFont`, `panelTitleFont`, `homeTitleFont`, `numberFont`), `icons.{h,cpp}` (`Icons::get/icon/exists`, `IconTone`, `kToolIcons`/`kPaneIcons`, `toolIconName`), `icon_names.txt` | 2.1 |
| `widgets/` | `ToolButton` (`setCompact` — без минимума 32 px внутри компонента), `SegmentedBar`, `SearchField`, `PanelHeader`, `Card`, `ListRow` (рисует сама по `rowColors`), `Chip`, `Toast` (`notify(host, …)`), `EmptyState`, `MessageBar`, `ColorSwatch`, `NumberField`, `PropertySheet` (2.5: `#undef PropertySheet` — макрос `PropertySheetW` из `<prsht.h>`/`windows.h` в unity-сборке; повтор — в `properties_dialog.cpp`, `xmp_editor.cpp`), `ZoomBox`, `FloatingBar` (основа QuickBar), `RowDelegate` (2.4b: строка 32 для `QListWidget` по `rowColors`, роли `kDetailRole`/`kDimmedRole`), `tone.h` (`Tone`, `repolish`), `showcase.{h,cpp}` (`createShowcase`, `createShowcaseWindow`, `forceShowcaseState`) | 2.1 |
| `view/` | `layout.h` (`layoutPages(sizes, LayoutOptions{mode, coverPage, zoom, rotation, gap})` → `PageLayout`: ряды, `slot(page).rect` в логических px, `contentSize(row)`, `pageAt`/`nearestPage`/`pagesIn` двоичным поиском; `pageModeFromPdf`), `page_view.h` (`PageView`: `setDocument(session, pool, cache)` или `setPageSizes`, режимы, `setZoom(z, якорь)`/`zoomIn`/`fit(FitMode)` (липкая), `rotateView`, `goToPage`/`goToPagePoint`, `viewState`/`goBack`/`goForward`, автопрокрутка, координаты `pageTransform(page)` — пункты страницы → вьюпорт, `viewportToPage`, `visiblePages(margin)`; `tilePainter()`, `overlays()`, `selectionOverlay()`/`searchOverlay()`/`annotationOverlay()`, `tools()`), `view_history.h` (50 видов), `tile_painter.{h,cpp}` (`paintPage`, `schedule`, `invalidatePage`/`invalidateAll`/`resetDevice`, `requestLog`, `visibleComplete`, сигнал `pageFailed`), `overlay.h` + `overlay_stack.cpp` (`Overlay`, `OverlayStack` по z, `QuadOverlay`, `RectOverlay`), `tool_controller.{h,cpp}` (`ToolController`: `setTool`, `cycleZoomTools`, `reset`, `finished`, `setPinned`, `setSingleKeyShortcuts`, `setEditable` (2.5: только чтение — «Выбор объектов» не двигает и не удаляет), сигналы `toolChanged`/`snapshotTaken`/`selectionChanged`), `tools/` (`view_tool.h` — `ViewTool` и классы инструментов, `tool_panels.h` — `ToolWindow` (до 2.3 — `ToolPanel`), `LoupePanel`, `PanZoomPanel`; `hand`, `select` (2.2b: `copySelection`, `Caret`/`caretRect` — клавиатурный курсор, `Shift+стрелки`), `marquee_zoom` (+ `RubberBandTool`), `loupe`, `pan_zoom_window`, `snapshot`; 2.2b — `annot_select` (`AnnotSelectTool`: `selection()`, `setSelection`, `deleteSelection`, `frames()`, `handleAt`, `annotList`; Esc — `ViewTool::escape`)), `link_layer.{h,cpp}` (2.2b: `LinkLayer` — ссылки по страницам задачей, `prefetch` видимых при отрисовке, `linkAt`, `focusNext`/`focused`/`clearFocus`, рамка фокуса — слой z 40); в `PageView` с 2.2b — `linkLayer()`, `activateLink` + сигнал `linkActivated(LinkAction)` (GoTo/Named — переход, XYZ-масштаб и Fit/FitH; GoToR/URI — решает окно; Launch — никогда), `focusNextPrevChild` — Tab по ссылкам, Enter — переход, `setSearchHits`/`setCurrentSearchHit`/`clearSearchHits` (`QuadOverlay::setCurrent` — несколько квадов), `ensureVisible(page, rect, margin)`; в `ToolController` — `annotSelect()`, `followsLinks()` (Выделение, Рука, Панорамирование), сигналы `annotSelectionChanged`, `annotContextMenu(QMenu*)`; 2.5b — `view_style.h` (`ViewStyle::fromSettings` — замена цветов `reading/*`, сглаживание и тонкие линии `pageDisplay/*`, `overprintPreview`, рамки; `styleFor(pdfx)`, `recolorFor(mode)`, `paperColor`), `TilePainter::setStyle` (бумага `paperColor`, прежние растры не подменяют), `PageView::setRenderStyle`/`renderStyle`, `boxes_overlay` (`BoxesOverlay` — рамки выпуска, обрезки, иллюстрации, z 35, `PageView::boxesOverlay()`), `ToolController::copyAllowed` (право «копирование») | 2.2, 2.2b, 2.5b |
| `tools/` | 2.3: `tool.h` (`Tool`: `id`, `title`, `icon`, `stage`, `isStub`, `activate(DocumentView&)`/`deactivate`, `panel` — виджет владеет инструмент, `commands` → `ot::Command`, `cursorFor`), `tool_registry.{h,cpp}` (`ToolRegistry::instance()`: `add` — тот же id заменяет, `tools`/`listed` — порядок эталона 6.2, закреплённые первыми, `activate`/`deactivate`/`setView`, `pinned` — `tools/pinned`), `stub_tool.h`, `builtin_tools.cpp` (`registerBuiltinTools` — 16 регистраций; зовут `main.cpp` и `main` ui-тестов); 2.6 — `print/` (`PrintTool` — выбор открывает диалог по `ToolRegistry::activated`, панель «Печать…», «Печать в
PDF…», «Установки печати…»; `PrintDialog(view, pages)` — все группы эталона 3.3, план сразу (`makePrintPlan`), 0 страниц и
неверный диапазон — «Печать» серая, «Свойства…» — `QPrintDialog` на своём `QPrinter`, «Microsoft Print to PDF» — файл по
`print/pdf*`, `setFileChooser`, `print()` → `PrintRunner`; `PrintPreview` — `PrintJob::preview` 96 dpi в пуле, поколения,
листание, стороны буклета; `PrintRunner` — `QtConcurrent`, `BackgroundTasks`, тост, `setNotify(false)` в тестах); 3.4 — `edit/`
(`EditTool`, `EditMode`, `FormatPanel`, `edit_script`; см. «Режим правки на странице»); 4.6 — `certificate/` (`CertificateTool` — id `certificate`, панель и команды меню «Подпись» `sign.digital`/`sign.certify`/`sign.certifyInvisible`/`sign.timestamp`/`sign.validateAll`/`sign.identities` (`sign.fillSign` — заглушка этапа 5 в `menus.cpp`), `blockReason(Action)`, `startSign`/`startCertify(visible)`/`startTimestamp`, `placeSignature(page, rect, field)`, `verifyAll`, `showIdentities`, `offerLtv` — полоса `ltvOfferBar`, `placementOf(PageView&)`, ответы тестов — `setFileChooser`/`setTimestampPrompt`; `sign_placement` — `SignPlacementTool` (`RubberBandTool` в слоте `ViewToolId::Sign`, однократный, `setFields` — пустые поля пунктиром, сторона < `kMinSide` 12 pt — щелчок по полю под точкой); `sign_dialog` — `SignDialog` (`Mode` Sign/Certify, `Context`, удостоверения `IdentityStore`, внешний вид с предпросмотром, «Расширенно» — уровень и служба времени, `runReview` — «Проверить документ», `submit` — пароль проверяется в диалоге, `identity()`), `certify_dialog` — `CertifyDialog`, `new_identity_dialog` — `NewIdentityDialog`, `appearance_dialog` — `AppearanceDialog`, `warnings_dialog` — `WarningsDialog`, `timestamp_dialog` — `TimestampDialog`, `sign_texts` — `SignTexts` (строки ошибок, статусов, вердиктов, `Issue`, предупреждений, `keyUsage` — только здесь), `sign_script` — `registerSignScriptActions`; см. «Интерфейс подписей»); по папке на
инструмент Acrobat: `print/ edit/ organize/ comment/ fillsign/ prepareform/ create/ combine/ export/ protect/ compress/ certificate/ scan/ accessibility/ javascript/ measure/` | 2.3 и далее |
| `panes/` | 2.4: `nav_page.h` (`NavPage`: `paneId`, `title`, `setDocument(DocumentView*)`, `activated` — читать, когда видна), `thumbnails_pane` (`QListView` + модель и делегат: миниатюра 160 только у рисуемых строк — display list задачей, растр в пуле окна приоритетом ниже тайлов, `requestedPages` для тестов; текущая, рамка видимой области — тащится; выбор; mime `application/x-acrobatya-pages`; 2.6 —
`selectedPages` → `DocumentView::setSelectedPages`, меню «Печать страниц…» — `pagesForMenu`/`printPages`), `bookmarks_pane` (`QTreeView` над `QStandardItemModel`, уровень — по раскрытию, заглушка «…», `levelLoaded`; 2.6 — меню
«Перейти» / «Печать страниц…»: `bookmarkPages` — до следующей закладки того же или более высокого уровня, `printBookmark`), `layers_pane`, `attachments_pane` (`setConfirm`, `setSaveChooser`, `openPdfRequested`, `trust/allowOpenAttachments`, `tempFolder`), `destinations_pane`; дальше — комментарии (5.4), поля (6.3); 4.6 — `signatures_pane` (`SignaturesPane` — id `signatures`: итог `DocumentView::signatures()` деревом «Подпись N: имя (статус)» с причинами, подписантом, временем, меткой, уровнем PAdES, LTV, изменениями; пустые поля — «Подписать поле…»; «Проверить все», «Параметры ▾»; `menuFor(field)` — пункты по objectName `sigGoTo`, `sigVerify`, `sigVerifyAll`, `sigSignedVersion`, `sigAddLtv`, `sigClear`, `sigProperties`, `sigCertificate`, `sigSignField`; `goToField`, `verifyAll`, `showProperties`, `showCertificate`, `viewSignedVersion` → `MainWindow::openSignedVersion`, `addValidationInfo`, `clearSignature`/`canClear`, `signField`; `setConfirm` — тесты; без проверки при открытии проверяет сама при показе; перенос строк — `WrapDelegate` + `doItemsLayout()` в `resizeEvent`); 2.5 — `find_bar` (`FindBar` на `FloatingBar` — панель «Найти» вкладки в правом верхнем углу вида, `DocumentView::findBar()`: `activate`, `find(text, options, focusPage, focusChar)`, `startSearch`, `findNext`/`findPrevious`, `dismiss`; совпадения из потока документа — общий буфер, опрос раз в 100 мс; > 0,5 с — фоновая задача; `noText` — «Нет текста — распознать?»; сигнал `advancedSearchRequested`) | 2.4, 2.5 и далее |
| `dialogs/` | диалоги по списку эталона 6.3 (диалог «Печать» — `tools/print/print_dialog.h`, 2.6); 2.5 — `search_window` (`SearchWindow`: «Расширенный поиск», не модальное, `Scope` Current/Open/Folder, `start`/`stop`, `rows()` — `Row` (текст, закладка, вложение, зашифрован, не открылся, нет текста), `toCsv`/`exportCsv`, `suggestions`, `openRow`; папка — `FolderSearch`, открытые — `search::runAll`), `properties_dialog` (`PropertiesDialog(view, readOnly)`: вкладки `Tab`, `showTab`, `isLoaded`/`fontsLoaded`, `changes()` → `props::apply`; открывает `MainWindow::showProperties`), `xmp_editor` (`XmpEditor`: `load`, категории Описание / Пользовательские / Дополнительно / XMP — возвращает значения в «Свойства»), `save_as_dialog` (`SaveAsDialog`: путь, способ записи, параметры полной записи → `SaveOptions`; `setFileChooser` — тесты), `password_dialog` (`PasswordDialog(имя, validator)`: три попытки, `remember`, `exhausted`), `recover_dialog` (`RecoverDialog`: `Choice` Later/Recover/Discard, сигналы `recoverRequested`/`discardRequested`); 2.5b — `link_confirm_dialog` (`LinkConfirmDialog(Kind::Url|File, target, host)`: «Открыть / Разрешить для сайта / Запретить», `choose`, `choice`); 4.6 — `signature_properties_dialog` (`SignaturePropertiesDialog(SignatureInfo, versions)`: `Tab` Summary/Document/Signer/DateTime, `showTab`; сигналы `certificateRequested`, `verifyRequested`, `signedVersionRequested`), `certificate_viewer` (`CertificateViewer(chain, revocation)`: цепочка от корня, `select(index)` — 0 подписант, `Tab` Summary/Details/Revocation/Trust/Policies, «Детали» — `certDetails`; `editTrust` → `TrustDialog` → `TrustService::edit`, сигнал `trustChanged`; `setTrustPrompt` — тесты), `trust_dialog` (`TrustDialog(cert, flags, existing)` — флаги `kTrust*`, пишет зовущий; показывает SHA-256, комментарий `.h` — «SHA-1»); диалоги подписания — `ui/tools/certificate/` | по сессиям, 2.5, 2.5b, 4.6 |
| `shots/`; 2.5b — `trust_scenes.cpp` (`protected-view` — копия с `Zone.Identifier`, `link-confirm`), `render_scenes.cpp` (`view-night`, `view-high-contrast`, `view-overprint`, `view-boxes-overlay` — стиль вкладке, не в settings.ini), в `prefs_scenes.cpp` — `preferences-page-display-full` | 2.3: `shell_scenes.cpp` (`shellScenes()` — `shell-document`, `shell-two-tabs`, `all-tools`, `all-tools-collapsed`, `quick-bar`, `menu-open`; `homeScenes()` — `home-empty`, `home-recent`, `home-files`; `homeScene` — Главная без недавних); 2.4 — `pane_scenes.cpp` (`pane-*`), `status_scenes.cpp`, 2.5b |
(`status-bar`, `message-bar-stack`), `mode_scenes.cpp` (`read-mode`, `full-screen` — окно полного экрана без главного); 2.4b — `prefs_scenes.cpp` (`preferences-general`, `preferences-page-display`, `palette`: окно верхнего уровня встраивается виджетом с фоном и тенью); 2.5 — `search_scenes.cpp` (`find-bar`, `search-window`), `dialog_scenes.cpp` (`properties-description`, `properties-fonts`, `properties-initial-view`, `xmp-editor`, `password-dialog`, `recover-dialog`, `pdfa-bar`); 2.6 — `print_scenes.cpp` (`print-dialog`, `print-dialog-booklet`, `print-dialog-poster`, `print-preview-nup`,
`print-from-thumbnails`; принтеры — `demoPrinters` из `runShots`), в `prefs_scenes.cpp` — `preferences-print`; общие `embedWindow`/`embedCentered`/`sceneDocumentReady` — в `scenes.h`;
`withMainWindow` ставит «Не сохранять» на закрытие и (2.4) сбрасывает состояние панелей прошлой сцены; `scenes.{h,cpp}` — таблица `SCENES` (`sceneTable()`: имя, функция окна, закреплённые тема/масштаб, `ready` — кадр готов для асинхронного растра, 2.2b), `ShotOptions`, `runShots`, `makeScene`, `waitSceneReady`, `sceneFileName`, `withMainWindow(setup)`; `fixtures.cpp` (`sourceRoot`, `sceneFixture`); `view_scenes.cpp` (2.2b: `viewScenes()` — `view-single`, `view-continuous`, `view-facing`, `view-cover`, `view-rotated`, `view-zoom-400`, `view-dark`, `view-selection`, `view-loupe`: `PageView` на фикстуре во весь кадр, настройка после показа); 4.6 — `sign_scenes.cpp` (`signScenes()`: `sign-dialog`, `certify-dialog`, `sign-appearance-dialog`, `sign-warnings`, `sig-bar-valid`, `sig-bar-problems`, `pane-signatures`, `certificate-viewer`, `preferences-identities`, `preferences-trusted`, `preferences-tsa`, `preferences-verification`, `preferences-sign-creation`; тёмные двойники `-dark` (`ThemeChoice::Dark`) — `sign-dialog`, `sig-bar-problems`, `pane-signatures`, `certificate-viewer`, `preferences-trusted`; сцены подписывают копию отчёта (ASCII-имя) при снимке и доверяют корню PKI, `sig-bar-*` ждут видимую кнопку полосы) | 0, 2.1+, 2.2b, 4.6 |

### Остальное

| Где | Что |
|---|---|
| `tests/unit/` | gtest, метка `unit`, < 1 с, данные — `tests/data/mini/*.pdf` (свои, ≤ 50 КБ); `support/test_pdf.h` — PDF в памяти (`makePdf`, `makeHeavyPdf` — секунды рендера на 600 dpi, `makeMixedPdf` — текст/кривые/картинки, `makeImagePagesPdf` — 200 страниц с картинками), `support/qt_app.h` — `QCoreApplication` и `waitUntil` для сигналов из потоков, `support/pdf_objects.h` — объекты PDF текстом («qpdf --json в миниатюре»: сравнение после undo и записи), `support/test_edits.h` — правки в операциях (`tenEdits`, `deletePage` — тяжёлая, `addNote`, `setRotate`, `putRootName`); 4.1 — `support/pki.h` (`pkiDir`, `readPki`, `pkiCert`/`pkiKey`, `pkiRootStore`, `localTsaReply(query)` — служба времени PKI в процессе для `FakeHttpFetcher`, `opensslExe` — внешняя проверка `openssl cms -verify -cades` / `ts -verify`, `runProcess` — с `OPENSSL_WIN32_UTF8=1`), 4.2 — `support/sign_support.h` (подписи файла и проверка CMS, см. рецепт «Подпись документа»), `support/tsa_server.h` (`TsaServer::start(&reason)` — `tools/pki/tsa_serve.py` через `OTTISK_PYTHON` окружения ctest, порт — первая строка stdout; нет Python/openssl — `GTEST_SKIP() << reason`); `tsa_compat.DISABLED_public_servers` — ctest `compat.tsa.public`; отключённые тесты-помощники `saver_compat.DISABLED_*` (файлы для `compat.save`) и `autosave_child.DISABLED_*` (дочерний процесс для `taskkill`) |
| `tests/ui/` | QTest offscreen, метка `ui`: набор на файл — `<имя>_test.cpp` с `OT_UI_TEST(Класс, "<имя>")` (`support/ui_test.h`) → ctest `ui.<имя>` без правки CMake; один `main` (`support/ui_main.cpp`, QApplication + светлая тема, `ui_tests <имя> [аргументы QTest]`, `--list`); `support/contrast.h` — WCAG; наборы `main_window`, `theme`, `icons`, `widgets`, `shots`, `i18n`, `page_view_layout`, `page_view_render`, `tools`, `annot_select`, `page_view_text` (2.2b), 2.3 — `main_window`,
`single_instance`, `tool_registry`, `tool_panel`, `home`, `commands`, 2.4 — `panes`, `status_bar`, `message_bar`, `modes`, 2.4b — `preferences`, `palette`, `script_runner` (сценарии `tests/scripts/*.json`, exe `--script`), `open_all`, 2.5 — `find`, `search_window`, `properties`, `xmp_editor`, 2.6 — `print_dialog`, `print_cli` (exe `/pt` под `OTTISK_FAKE_PRINTER`), `scale_factor`
(exe `--shots` под `QT_SCALE_FACTOR` 1.25/1.5 — тайлы без швов), `ShotsTest::stage2` (тёмные сцены этапа 2), 3.2 — `dwrite` (живой DirectWrite), `save_flow` (ответы модальных диалогов — `MainWindow::setPasswordPrompt`, `SaveFlow::setSignaturePrompt`/`setSaveAsPrompt`; QTRY_* — только в функциях без возвращаемого значения), 4.6 — `sign_dialog`, `signatures_pane`, `sign_settings`, `script_sign` (`tests/scripts/sign.json`), `ShotsTest::stage4` (сцены подписей, светлые и тёмные); `support/sign_ui.h` — `PkiNetwork` (сеть подписей процесса без Python и сети: служба времени PKI отвечает на любой `application/timestamp-query`, в т. ч. по адресу DigiCert, OCSP/CRL/ЦС — `http://pki.acrobatya.test/…`; `setTsaDown`), `signFile(path, SignFileOptions)`, `tamperFile`, `pkiTrust`, `trustPkiRoot`/`untrustPkiRoot`, `verifyFile`, `copyFixture`, `pkiPfx`; слоты подписей зависят от порядка (удостоверение и пароль — в state.sqlite каталога данных набора) (main
ставит `FakeShellLauncher` на весь прогон) (набор, который пишет настройки или `state.sqlite`,
ставит свой `OTTISK_DATA_DIR` = `build/<пресет>/test-data/ui-<имя>` в `initTestCase`; окно, которое закрывается с
правками, — `setCloseConfirm`) (2.2: вид с
документом — `DocumentSession` на фикстуре, дорисовка — цикл `repaint` до `tilePainter().visibleComplete()`; правка в
обход ui — `session.edit` с MuPDF прямо в тесте); снимки виджетов → `build/<пресет>/ui-shots/`; кириллицу печатать `setText`, не `QTest::keyClicks` (только Latin-1) |
| `tests/engine/` | gtest на корпусе по `<корпус>/index.json` (`open_corpus_test.cpp`: тег → части по 200 файлов, `progress:`), `mutants_test.cpp`; 3.1 — `content_roundtrip_test.cpp` (тег `text-edit` частями по 50: `serialize()` байт в байт, сверка с fz_stext ≥ 90 % страниц части; `OTTISK_CROSS_TRACE=1` — расхождения), `edit_stability_test.cpp` (пустая правка всех файлов тега в 4 потока, доля ≥ 90 %, отчёт `build/edit-stability.json`); 3.2 — `font_mixed_test.cpp` (набор «Привет, world! — 2026 €» в латинское подмножество против строки-эталона, файлы — `build/<пресет>/font-mixed/`); 4.3 — `sign_matrix_test.cpp` (`engine.sign.matrix`: 12 подписанных файлов в `build/<пресет>/sign-matrix/{bb,bt}` — openssl и pyHanko; `sign_live.DISABLED_windows_my` — живая подпись по `OTTISK_LIVE_CERT`); 4.4 — `dss_corpus_test.cpp` (`engine.sig.dss`: 124 файла EU DSS тегов sig-*, якоря — самоподписанные сертификаты файла, retroactive без сети, `{kind, intact, trusted, modified, level}` против `tests/fixtures/sig/dss_expectations.json` ≥ 90 %, вердикты — `build/<пресет>/dss/ours.json`; `compat.sig.pyhanko` — `tools/compare_pyhanko.py compare`, отчёт `build/<пресет>/dss/report.md`); зонд `support/probe.h` (открыть → 32 × 32 → закрыть, `QuietLog`, `isBrokenFileCode`, `isMemoryFailure`); предел памяти процесса `support/memory_cap.cpp` (Job Object, `OTTISK_TEST_MEMORY_LIMIT_MB`, 2048 / ASan 6144, 0 — выкл.), `PROCESSORS 2` (ASan — 4); `OTTISK_CORPUS_TRACE=1` — печать каждого файла перед зондом (найти файл падения); срок рендера зонда `OTTISK_PROBE_RENDER_TIMEOUT_S` (15 / ASan 45 с); пропуск `tests/corpus/slow.json` — файлы, которые сам MuPDF рисует минутами (`tools/scan_slow_corpus.py --write`), раздел `asan` — только под ASan (процесс упирается в предел памяти); падение процесса — код и стек в stderr (`support/crash_report.cpp`); метки `engine;slow`, `FIXTURES_REQUIRED corpus;mutants` (`engine.corpus.verify` — `fetch_corpus.py --verify --dry-run`, 77 — пропуск; `engine.mutants.generate` — `make_mutants.py` в `build/<пресет>/gen`), `TIMEOUT_AFTER_MATCH 120;^progress:` |
| `tests/render/` | `render_test.cpp` по `expected/expected.json`, эталоны `expected/<корпус>/<файл>.<стр>.<dpi>.png`, `files.json` — источники |
| `tests/scripts/` | 2.4b: сценарии `--script` (`open_zoom_shot.json`, `property_undo.json`, `unknown_command.json`; пути — от каталога сценария, файлы — в `${OUT_DIR}`); сессии-владельцы кладут сюда свои (рецепт «Новое действие `--script`»); 4.6 — `sign.json` (сценарии 4 и 5 эталона, сертификация P=2 и метка документа; без подмены службы времени, как в `ui.script_sign`, нужна сеть — DigiCert) |
| `tests/e2e/`, `tests/compat/`, `tests/bench/`, `tests/build/` | сценарии эталона 2 без UI; `compat_check.py` (SKIP 77; файлы или каталоги); ctest `compat.save.generate` → `compat.save` / `.signed` (pyHanko INTACT) / `.encrypted` по `build/<пресет>/compat/save` (метки `compat;slow`); `compat.fonts.generate` → `compat.fonts` (3.2: `font_embed_compat.DISABLED_write` → `build/<пресет>/compat/fonts`); `compat.rtf.generate` → `compat.rtf` (`tests/compat/rtf_word_check.ps1`: Word COM открывает RTF из `build/<пресет>/compat/rtf`, «Привет» в `Content.Text`; нет Word — 77); `bench.cpp` → `bench.exe <файл\|hello\|pdf32000\|big-1200\|images-200\|enc-r6\|search\|ui-paging>` (`ui-paging` — `bench_ui.cpp`, 2.2b: PageView offscreen 1440 × 900 над PDF32000_2008, `first_frame_ms`, `page_cold_ms`, `page_warm_ms` — QBENCHMARK, числа только пишутся; ctest `bench.ui.paging` и в Debug — JSON в `build/<пресет>/bench/`, в Release — в `build/bench/`; `bench` с 2.2b линкуется с `ottisk_ui_lib` и Qt Test) (`search` / `--search <образец>` — `search_all_ms`, порог 3 с только в Release) (только Release: ctest `bench.engine.*`, JSON в `build/bench/`, сравнение — `tools/bench_compare.py docs/bench/baseline-<прошлый>.json "build/bench/*.json"`: +25 % медианы, шум < 5 мс, цели 5.8); `install_test.py` (метка `build`) |
| `tests/corpus/manifest.json` | корпуса `{id, url\|git, sha256, license, tags[], expect}` → `C:\Tools\ottisk\corpus` (`OTTISK_CORPUS`) |
| `tests/fixtures/` | свои фикстуры ≤ 50 КБ и пермиссивные (`LICENSES.md`): 3.1 — `edit/` (`tools/gen/make_edit_fixtures.ps1` — Word COM, затем `make_edit_fixtures.py` — свои PDF с точной геометрией и `expected.json`: спаны и блоки; 16 своих — латиница, кириллица CID, Type1 /Differences, base14 без /Widths, TJ-кернинг, Tz/Tc/Tw/Ts, повёрнутый, два потока /Contents, форма, две и три колонки, четыре выравнивания, inline image; 5 из Word — абзацы, списки, колонки, таблица, надпись снизу вверх; `lo-paragraphs.pdf` — только с LibreOffice; 3.2 — `edit/fonts/` (`tools/gen/make_font_fixtures.py`: `fonts.pdf` — девять шрифтов F1–F9 с ожиданиями в `fonts.json`, `mixed-latin.pdf` — абзац простым TrueType Arial только ASCII, `mixed-expected.pdf` — он же со строкой-эталоном «Привет, world! — 2026 €»)); 2.6 — `pages/` (`tools/gen/make_pages.py`: `mixed-sizes.pdf` — A4, A5, A4 альбомная, A1; `print-layers.pdf` — слой `/PrintState /OFF` и обычный), `print/nup4_a4.json` (раскладка n-up вручную); `annots/ forms/ enc/ sig/ images/ scans/ office/ scenes/ demo/ imgcmp/ (метрики imgcmp) view/ (2.2: layout-mixed.pdf — размеры, /Rotate, /PageLayout; layout_expected.json — раскладка вручную; 2.2b: links.pdf + links.json — ссылки всех видов) regress/ (падения фаззера → unit.regress.load) pki/ (4.1) text/ (1.4: search.pdf, vectors.pdf, truetype-nonembedded.pdf, text.json — строки генератора, stext_expected.json — слова PDFium; 2.5: find.pdf — «печать» 12 раз на трёх страницах (5 + 4 + 3), «печати», «впечатление»)` |
| `tools/` | `vcenv.cmd`, `verify.py`, `verify_r1.py` (рубеж Р1 — автоматическая часть M1: корпус `--open-all` по тегам и под ASan, листание и память Release, undo/redo, автосохранение после `taskkill`, тайлы при `QT_SCALE_FACTOR`, печать по эталонам раскладки, `/pt`, сцены; отчёт `.queue/r1/report.md`; образец для `verify_r<N>.py` следующих рубежей), `build.ps1`, `build_lock.ps1`, `fuzz.ps1` (фаззер 60 с / 10 мин), `shots.ps1`, `lint.ps1` (`-Changed`/`-All`, `-Jobs N` — пачки clang-tidy параллельно), стражи `*_guard.py` и `scope_check.py`, `scope_report.py`, `imgcmp/`, `render_refs.cpp`, `review_refs.py` (`--shots --sheet 16 --main`), `main_scenes.txt`, `fetch_corpus.py`, `scan_slow_corpus.py`, `cl_launcher.py` (обёртка компилятора: русский префикс `/showIncludes` → «Note: including file:», иначе Ninja не видит зависимостей от заголовков), `compat_check.py`, `bench_compare.py`, `analyze_summary.py`, `copy_icons.py`, `gen/` (генераторы фикстур), `get-tessdata.ps1`, `install-qt.ps1`, `mupdf-build.ps1` + `ottisk-md.props`, очередь: `queue-lib.ps1`, `test_reset_parse.ps1`, `test_guard.ps1` |
| `installer/` | `ottisk.iss` (UTF-8 BOM, per-user), `app.ico`, `app.rc`, `AcroBatya.manifest` (UTF-8 ACP, longPathAware), `licenses/` |
| `assets/` | `icons/` (только нужные Lucide + `LICENSE`; списки `icons.txt` и `src/ui/theme/icon_names.txt`; встроены ресурсом `:/icons` в `ottisk_ui_lib`), `stamps/`, `help/ru`, `help/en` |
| `i18n/` | `ottisk_ru.ts`, `ottisk_en.ts` (`qt_add_translations` в `CMakeLists.txt`: `.qm` с qtbase — ресурс `:/i18n` у `AcroBatya` и `ui_tests`), части `en-parts/` для перевода агентами (9.2) |

### Стражи (ctest `unit.guard.*`, запуск `"$env:OTTISK_PYTHON" -X utf8 -I tools/<имя>.py --root .`; 0 — зелёный, 1 — красный, 77 — пропуск)

`i18n_guard.py` (кириллица только в `tr()`/`QT_TR_NOOP()`/`translate()`; в `core`/`services` — только комментарии; контекст
`translate` — ASCII; `--self-test` → `unit.guard.i18n_selftest`), `copy_icons.py --check` (`unit.guard.icons`: SVG списков на
месте, лишних нет, LICENSE), `fztry_guard.py` (`fz_try` только в `mu::call`), `boundaries_guard.py` (с 4.3 — Windows SDK (`windows.h`, `wincrypt.h`, `ncrypt.h`…) не в заголовках core; с 4.1 — и сеть: `QNetworkAccessManager`, `OSSL_HTTP_*`, `OCSP_sendreq_*`, WinHTTP/WinINet только в `src/services/http_fetcher.cpp`)
(слои; с 2.4 — `ShellExecute*`/`QDesktopServices::openUrl` только в `src/services/shell_launcher.cpp`), `scope_check.py` (`docs/scope.json` ↔ `docs/features.json` ↔ ID пунктов плана), `encoding_guard.py`
(BOM у `.ps1`/`.iss`), `no_registry_guard.py` (настройки не в реестре; 2.4b — и `QSettings(организация…)` без IniFormat: Qt открывает его в NativeFormat при любом `setDefaultFormat`), `format_guard.py` (clang-format), 2.3 —
`hotkeys_guard.py` (`unit.guard.hotkeys`: `AcroBatya.exe --commands-list` ⊆ таблица эталона 6.8 из `design.md`; id →
строка таблицы — словарь `ACTIONS`; конфликт — только при разных `when`; `--complete` — полнота, 9.2; `--table` —
печать разобранной таблицы; ctest с `--exe` своего каталога сборки, в `PY_GUARDS` `verify.py` не входит — там стражи
идут параллельно сборке).
PowerShell-стражи очереди `tools/test_reset_parse.ps1` и `tools/test_guard.ps1` запускает `tools/verify.py`.


## Рецепты

### Новый инструмент в `ToolRegistry` (с 2.3)

1. Папка `src/ui/tools/<папка>/` (имя — из эталона 6.2: `comment/`, `organize/`, `protect/`…), класс
   `<Имя>Tool : Tool` в `<имя>_tool.{h,cpp}`. Интерфейс `Tool` (`src/ui/tools/tool.h`): `id()`, `title()`
   (`tr()`), `icon()` (имя Lucide), `stage()` (для серых, ещё не готовых), `activate(DocumentView&)`/`deactivate()`,
   `panel()` (виджет левой панели), `commands()`, `cursorFor()`.
2. Регистрация — заменить строку заглушки `registry.add(stub("<id>", …))` в `src/ui/tools/builtin_tools.cpp` на
   `registry.add(std::make_unique<<Имя>Tool>())` (её зовут `main.cpp` и `main` ui-тестов); оболочка («Все инструменты»,
   меню «Инструменты», QuickBar) не правится — порядок из эталона 6.2 держит `ToolRegistry`. Панель — `panel()` (виджет
   создаёт и держит инструмент; `ToolPanel` встраивает его и отдаёт назад), включение — `activate(DocumentView&)` при
   выборе и смене вкладки, `deactivate()` — при «◀ Все инструменты» и уходе вкладки.
3. Иконка — имя Lucide в `src/ui/theme/icon_names.txt` + `"$env:OTTISK_PYTHON" -X utf8 -I tools/copy_icons.py`
   (копирует только нужные SVG в `assets/icons/`); неизвестное имя — `qFatal` в Debug и красный `ui.icons.exist`.
4. Состояния мыши — через `ToolController` в `PageView` (не свой обработчик событий поверх виджета): инструмент правки
   добавляет свой `ViewTool` (рецепт «Новый инструмент вида» ниже) и включает его из `activate()`.
5. Логика — в `core`/`services` с тестами первыми; инструмент только вызывает `DocumentSession`.
6. Команды и клавиши — рецепт «Новая команда и клавиша»; сцена `tool-<имя>` и просмотр PNG; ui-тест в `tests/ui/`.

### Новый инструмент вида (`ViewTool`, с 2.2)

1. Класс в `src/ui/view/tools/view_tool.h` (наследник `ViewTool` или `RubberBandTool` для рамки), реализация —
   `tools/<имя>.cpp`; id — в `ViewToolId`, экземпляр — в конструкторе `ToolController` (и `kViewToolCount`).
2. Мышь — `mousePress/Move/Release/DoubleClick` (координаты вьюпорта; страница — `view.pageAtViewport`, пункты —
   `view.viewportToPage`), клавиши — `keyPress`, свой слой — `paintOverlay` (вьюпорт; страница → вьюпорт —
   `view.pageTransform(page)`), курсор — `cursor(point, mods)` (иконки — `Icons::get`), Esc — `cancel()`; инструмент,
   у которого Esc сначала снимает выбор и не уходит, — `escape(cancelled)` → true (как `AnnotSelectTool`, 2.2b).
   Ходить по ссылкам (курсор-указатель, переход щелчком) — добавить id в `ToolController::followsLinks()`.
   Аннотации — выбор, перемещение, ручки, Del — уже в `AnnotSelectTool` (`controller.annotSelect()`): инструмент
   комментариев включает его или наследует; пункты контекстного меню — слот к `ToolController::annotContextMenu`.
3. Однократный (после действия — прежний инструмент, если не «закреплён», comment-42) — `oneShot() = true` и
   `controller.finished()` по готовности.
4. Документ — только через `DocumentSession` (`textPage`, `ClipboardIo::snapshot`, `annot::*` из `services/annot_ops.h`),
   ответы — `future.then(&controller, …)`; смена документа и правка страницы приходят в `ToolController::documentChanged`
   / `pageChanged` — сбросить кеши инструмента там.
5. Плавающая панель — наследник `ToolWindow` (`tool_panels.h`: заголовок с «Закрыть», правый верхний угол вьюпорта,
   токены `toolWindow*`); тест — `tests/ui/tools_test.cpp` (QTest-мышь в `view.viewport()`; одну и ту же точку
   второй `mouseMove` не доставит).

### Слой поверх страниц (`Overlay`, с 2.2)

Готовые — `view.selectionOverlay()` (выделение), `searchOverlay()` (совпадения, текущее — `setCurrent`),
`annotationOverlay()` (рамки): `setQuads(page, …)` / `setRects(page, …)` в пунктах страницы, потом
`view.viewport()->update()`. Свой слой — наследник `Overlay` (`paintPage` — painter обрезан рамкой страницы,
`paintViewport` — поверх всего), `view.overlays().add(&слой, z)` (поиск 10, выделение 20, рамки 30, инструмент 100);
владелец слоя убирает его `remove()` до своей смерти.

### Новая аннотация (ядро — 2.2/5.1, инструменты — 5.1–5.5)

1. `src/core/annot/annots.h`: тип в перечне подтипов, создание/чтение/правка через `mu::call`, значение-тип
   `AnnotInfo` для `ui` (никаких `pdf_annot*` наружу); внешний вид — `annot/appearance.h` (свой AP, не полагаться
   на чужие читалки).
2. Правка — только через `DocumentSession` и журнал (undo/redo); сохранение — правила `Saver` (инкрементально,
   подписи не ломать).
3. Тесты первыми: `tests/unit/annot_<тип>_test.cpp` — создать → сохранить → переоткрыть → свойства и рендер;
   XFDF round-trip (`annot/xfdf.h`) на матрице 28 подтипов (`tests/fixtures/annots/`, генератор `tools/gen/`);
   `compat`: `qpdf --check`, `pdfcpu validate -m strict`, pypdfium2 рендерит (`imgcmp --cross`).
4. Инструмент — в `src/ui/tools/comment/` (рецепт выше), свойства — диалог «Свойства комментария»,
   панель «Комментарии» — `src/ui/panes/`.
5. `OT_FEATURE("comment-NN")` в тестах; строка `CHANGELOG.md`.

### Новый диалог

1. `src/ui/dialogs/<имя>_dialog.{h,cpp}`, класс `<Имя>Dialog : QDialog`; собирается из компонентов
   `src/ui/widgets/` и токенов `src/ui/theme/tokens.h` — без своих цветов и размеров.
2. Строки — только `tr("…")` (русские исходные); мнемоники `&`, кнопка по умолчанию, `Esc` закрывает;
   `QAccessible`-имена у своих виджетов (`ui.a11y.names`); контекстная справка `?` → раздел `assets/help/ru/*.md`.
3. Файловые диалоги — нативные, в тестах `QFileDialog::DontUseNativeDialog`; модальных «подождите» нет —
   прогресс в строке состояния.
4. Данные — через `services` (настройки, `DocumentSession`), не прямые вызовы `core` из диалога.
5. Сцена `dialog-<имя>` (рецепт «Новая сцена»), ui-тест (открыть, заполнить, принять) в `tests/ui/`.
6. С 2.5: диалог документа — `open()` (окно-модальный, `WA_DeleteOnClose`; тесты и `--script` не стоят на `exec()`);
   где нужен `exec()` (пароль при открытии, «Сохранить как», предупреждение о подписях) — ответ подставляет функция
   (`MainWindow::setPasswordPrompt`, `SaveFlow::setSignaturePrompt`/`setSaveAsPrompt`). Данные — задачей `*_ops.h`
   (до загрузки — `setEnabled(false)`), долгое — по требованию (вкладка, кнопка); «ОК» пишет только изменённое одним
   шагом журнала (`props::apply`). Сцена — `embedCentered(w, диалог)` в `src/ui/shots/dialog_scenes.cpp` (как
   `properties-fonts`, `password-dialog`).

### Новая настройка (с 2.4b)

1. Ключ и умолчание — одна строка в `src/services/settings_keys.h` в группе своей категории
   (`general/`, `pageDisplay/`, `documents/`, `commenting/`, `forms/`, `editing/`, `signatures/`, `trust/`,
   `search/`, `reading/`, `ocr/`, `convert/`, `print/`, `hotkeys/`, `quickbar/`, `language/`): `inline constexpr BoolKey
   kИмя{"группа/ключ", умолчание};` (`IntKey` — с пределами, `StringKey` — с `choices` через «|», `ListKey`); чтение и
   запись — `Settings().get(settings::kИмя)` / `Settings().set(…)` (`src/services/settings.h`), не `QSettings` напрямую.
2. Хранение — `QSettings(IniFormat)` → `%LOCALAPPDATA%\AcroBatya\settings.ini` (никогда не реестр —
   `unit.guard.no_registry`); в тестах и сценах — `OTTISK_DATA_DIR`. Смена смысла ключа — миграция по `meta/version`.
3. Интерфейс — в категории диалога «Установки» (`src/ui/shell/preferences/<категория>_page.{h,cpp}`, наследник
   `PreferencesPage`: поля в `load(const Settings&)`, запись в `apply(Settings&)`; заглушку категории заменяет строка
   `addPage` в `PreferencesDialog`; действие на работающее окно — в `MainWindow::applyPreferences`; названия — как в
   русском Acrobat).
4. Тест: умолчание, запись-чтение, влияние на поведение; сцена категории Установок при новом виде.

### Новая сцена

1. Строка в таблице `SCENES` (`sceneTable()` в `src/ui/shots/scenes.cpp`, имя — `kebab-case`): функция окна — обычно
   `withMainWindow([](MainWindow& w) { … })`, которая доводит окно до нужного вида на фикстурах (`sceneFixture("demo/…")`
   — `tests/fixtures/scenes/` и `tests/fixtures/demo/`), без сети и данных человека. Тему/масштаб сцена закрепляет
   полями `theme`/`scale` (как `showcase-dark`, `showcase-large`) — тогда варианты `--theme`/`--scale` её пропускают.
   Растр приходит из потоков (вид страниц, миниатюры) — поле `ready` (кадр готов; `runShots` ждёт до 30 с); настройка
   вида, которой нужен размер вьюпорта (подгонки, прокрутка), — после показа (`ViewSceneHost` в `view_scenes.cpp`).
2. Снять: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\shots.ps1 -Only <имя> [-Dark] [-Large] [-Lang en]`
   → `.queue/screenshots/<имя>[-dark|-contrast][-large][-en].png` (offscreen, 1440 × 900, Segoe UI из `C:/Windows/Fonts`);
   `-All` — все сцены в пяти вариантах; список имён — `AcroBatya.exe --shots-list`.
3. **Посмотреть PNG** (`Read`) и сверить с эталоном 6: размеры 48/320/44/280/32, токены, подписи, иконки,
   кириллица. Главные экраны — и в тёмной теме, и дописать в `tools/main_scenes.txt` (30 главных сцен рубежа).
4. Пиксельного эталона у сцен нет; pHash к снимкам прошлого рубежа (`tools/review_refs.py --shots --sheet 16 --main`)
   — сигнал, не закон.

### Новый компонент, цвет или размер (с 2.1)

1. Цвет — только имя токена `Palette` (`tokens.h`, во всех трёх палитрах), размер — поле `Metrics` (и в `kMetricSizes`,
   если это размер); в коде экранов и компонентов чисел цвета нет — `Theme::colors()`, `Theme::sizes()`.
2. Компонент — `src/ui/widgets/<имя>.{h,cpp}`, класс в `ot::ui`; размеры — в `applyMetrics()`, подключённом к
   `Theme::themeChanged` («Крупные элементы» меняют их на лету); иконки — `Icons::icon(name, tone)` (перекрашиваются сами).
3. Вид — строки таблицы правил в `qss.cpp` (`ot--ui--<Класс>`, варианты — свойства `[tone="…"]`, после смены свойства —
   `repolish`); у правила с цветами — имя компонента и состояние: пара уходит в `componentColors()` и тест 4,5:1.
   Рисует сам (`paintEvent`) — цвета через функцию темы вроде `rowColors()`, её пары — тоже в `componentColors()`.
4. Витрина `showcase.cpp` — строка «компонент × состояния»; тест в `tests/ui/widgets_test.cpp`; снимок `showcase*`.

### Новая строка интерфейса и перевод (с 2.1)

Только `tr("…")` в `ui` (вне QObject — `QCoreApplication::translate("ot::ui::<Класс>", "…")`, контекст ASCII); `.ts`
обновляет `cmake --build build/debug --target update_translations` (сессия 9.2), `.qm` собирается сам и встроен в
`:/i18n`; окно, которое должно сменить язык без перезапуска, ловит `QEvent::LanguageChange` в `changeEvent`.

### Новый тест на корпусе

1. Файл `tests/engine/<тема>_test.cpp` (цель `engine_tests`, метки `engine;slow`, фикстура `corpus`);
   файлы — по тегу из `tests/corpus/manifest.json` (теги эталона 9.2: `annots`, `forms`, `xfa`, `enc-r2..r6`,
   `sig-b/t/lt/lta`, `pdfa-pass/fail`, `big`, `cyr`, `fuzz`, `office`, `scan`, `text-edit`, `tagged`, `trust`)
   из `OTTISK_CORPUS` (`C:\Tools\ottisk\corpus`).
2. Сессии и рубежи корпуса **не качают**: нет тега на диске — `GTEST_SKIP()` и строка в «Нужна ручная проверка»;
   новый тег — только этап 0 / человек (`fetch_corpus.py --verify` сверяет теги плана).
   **Память:** файлы-фаззы заставляют MuPDF выделять гигабайты — тест корпуса живёт в `engine_tests` (предел памяти
   процесса) или ставит свой; `ctest -L engine` — не больше `-j 6` (под ASan `-j 4`): 23.10.2026 десять шардов
   `fuzz` без предела исчерпали память и уронили Windows.
3. Ожидания — JSON рядом с тестом или в `tests/fixtures/` (для XL — до кода); каждый записанный тестом файл
   проходит минимум `qpdf --check` (`tools/compat_check.py`).
4. Маленький пермиссивный файл, нужный быстрому тесту, — копией в `tests/fixtures/` с записью в `LICENSES.md`.

### Задача документа и рендер тайлов (с 1.2)

1. Всё, что трогает `fz_document`/`fz_page`/`pdf_*`, — задачей `DocumentSession`: `post([](TaskContext& tc) { … },
   TaskPriority::…)` → `QFuture<T>`; правка — `edit(QStringLiteral("<операция по-английски>"), fn)` (fn идёт внутри
   `Journal::Operation` — рецепт «Правка с undo» ниже; после неё `revision++`, `documentChanged`, `pageChanged` для
   `tc.touchPage(n)`); в тестах — `postSync`. Задача использует `tc.cookie()` в
   `fz_run_*`/`Renderer::render` — тогда `cancelAll()` её прерывает. Ошибка — исключение в `QFuture` (`MuError`) и
   `taskFailed(TaskFailed{code, kind, message, hint})`; отмена — `MuError` с `ErrorKind::Aborted` и без сигнала.
2. ui не держит указатели MuPDF: только номер страницы, `TileKey`, `shared_ptr<const DisplayList>` (непрозрачен для ui) и
   `QImage`. `PageHandle` (`tc.page(n)`) живёт только внутри задачи.
3. Тайлы (в окне это делает `ui/view/tile_painter.cpp` — свой код рендера в ui не нужен): `session.displayList(page)` → `tileRectsFor(list->bounds(), zoom, dpr, rotation)` → `RenderPool::submit(TileRequest{
   makeTileKey(session.id(), page, zoom, dpr, rotation, tile, session.revision()), list, pageMatrix(…), tile.rect, dpr})` →
   результат в `TileCache::insert`; смена зума — `retarget(doc, видимые, приоритет)`; закрытие — `cancelDocument(doc)` и
   `TileCache::removeDocument(doc)`. Превью — `makePreviewKey` + `previewZoom(zoom)` + `insertPreview`.
4. Документы для пула — только на клонах `MuContext::global()` (так открывает `DocumentSession`): список из чужой семьи
   пул отвергает ошибкой. Мозаика тайлов совпадает с целым рендером бит в бит для текста и прямоугольников; у кривых и
   растянутых картинок MuPDF даёт отличия в доли пикселя на рёбрах (GEL режет рёбра по scissor) — швов нет, тесты
   `unit.render.tiles_mosaic_*` держат границы.

### Текст страницы, поиск и выделение (с 1.4)

1. Слой — только задачей документа: `tc.textPage(n, options)` (кеш LRU 16, правка страницы сбрасывает) или
   `session.textPage(n)` → `shared_ptr<const TextPage>`; значения (`chars()`, `lines()`, `words()`…) читаются в любом
   потоке, `stext()` — только контекстом семьи (`contextForThisThread`). Порядок чтения — опции по умолчанию; поиску
   и индексу хватает `SearchSession::textOptions()` (без SEGMENT — в разы быстрее).
2. Функции выделения MuPDF (`fz_highlight_selection`, `fz_copy_selection`, `fz_snap_selection`, `fz_copy_rectangle`)
   не заходят в struct-блоки — на слое с SEGMENT они пусты; выделять — `core/text/selection.h`. Вывод stext
   (`fz_print_stext_page_as_*`) и `fz_new_search` struct-блоки обходят.
3. Новые данные `state.sqlite` — шаг миграции в конце списка `state_db.cpp` (`exec(db, "CREATE …")`, ошибка — откат шага)
   и `kSchemaVersion + 1` (2.3: 2 — `session_tabs`, 3 — `recent`); запросы — под `StateDb::mutex()`.
4. Фикстура текста — `tools/gen/make_text.py`, эталон слов — `tools/gen/stext_expected.py` (pypdfium2, независимый
   источник; лигатуры PDFium раскладывает на буквы).

### Правка с undo и сохранение (с 1.3)

1. Журнал MuPDF включён с открытия: правка PDF вне операции — ошибка `FZ_ERROR_ARGUMENT`. Функция ядра, которая
   правит, сама открывает `Journal::Operation op(doc, "Add note")` (английский код — имя шага; подпись «Отменить: …»
   по-русски выбирает ui), правит через `mu::call`, отмечает `op.invalidate(page)` / `op.invalidateAll()`
   (структура страниц) / `op.markHeavy()` (страницы, картинки, редактор, уменьшение — `Saver` пишет файл целиком)
   и `op.commit()`; без `commit` (исключение) — откат. Внутри `DocumentSession::edit` она вложенная: шаг один, имя
   внешнее; из задачи — `tc.touchPage`, `tc.touchAllPages`, `tc.markHeavyEdit`.
2. `fz_page` через undo не держать; `pdf_update_object` в журнал не попадает (журналируются `pdf_dict_put*`,
   `pdf_update_stream`, `pdf_add_*`); трейлер — тоже не в журнале.
3. Запись — только `Saver`/`DocumentSession::save`: после записи документ переоткрыт, история пуста
   (`savePdfToMemory` — для тестов, тоже сбрасывает историю). Автосохранение — `AutosaveRunner::watch(session)`.
   С 2.5 в окне: запись — только `MainWindow::saveFlow()` (`SaveFlow::save`/`saveAs`: решение до записи, подписи,
   ошибки по-русски), автосохранение — `MainWindow::autosave()`; операция с перезаписью (этапы 7–9) — новый документ
   «Без имени» без undo и `SaveFlow::rewriteNotice(view, tr("…"))`; новый параметр «Сохранить как» — поле
   `SaveOptions` и флажок `SaveAsDialog`.
4. Тест: правка → `undo` → объекты как были (`support/pdf_objects.h`) → `redo`; запись → `compat.save` (артефакт в
   `saver_compat.DISABLED_write_artifacts`) — `qpdf --check`, `pdfcpu validate -m strict`, pypdfium2.

### Новая страница правой панели (с 2.4)

1. Класс `<Имя>Pane : NavPage` в `src/ui/panes/<имя>_pane.{h,cpp}`: `paneId()` (id из `NavPane::paneIds()`: comments,
   signatures, fields…), `title()` (`tr()`), `setDocument(DocumentView*)` — запомнить и, если страница не видна, только
   пометить «перечитать»; `activated()` — перечитать помеченное. Документ — задачами (`services/nav_ops.h` или своя
   `*_ops.h`), ответ — `future.then(this, …)` с номером поколения (вкладку могли сменить).
2. Заглушку заменяет `navPane().replacePage(new <Имя>Pane)` (окно зовёт её при создании; `StubNavPage` уходит).
3. Пусто — `EmptyState` (иконка панели из `kPaneIcons` + строка); кнопки внизу — ряд `paneButtons` (QSS — рамка сверху).
4. Тест — `tests/ui/<имя>_test.cpp` (окно, `navPane().showPane(id)`), сцена `pane-<id>`.

### Полоса сообщений и фоновая задача (с 2.4)

- Полоса над документом — `view->messageStack().showMessage(MessageKind::…, tr(…))` (одна на вид, порядок — по
  важности, тон — по виду; кнопки — `addAction(текст, id, primary)` + сигнал `actionTriggered`); своя — `add(tone, text)`.
  Новый вид — в `MessageKind` по месту важности (и строка в `ui.message_bar`).
- Долгая работа — `BackgroundTasks::shared().start(tr("…"), отмена)`, `setProgress(id, %)`, `finish(id)` — строка состояния
  показывает её сама (без модальных «подождите»).
- Внешнее (браузер, программа по умолчанию, Проводник, Параметры) — только `ShellLauncher::instance()`; в тестах —
  `FakeShellLauncher` (`ScopedShellLauncher`), проверка — `calls`. Адрес из документа (ссылка, закладка,
  `app.launchURL` 6.1, `SubmitForm`) — не сам, а `MainWindow::linkFlow().follow(view, action)` (подтверждение по сайту,
  защищённый просмотр, запрет `/Launch`).
- Чужой файл — `MainWindow::applyProtectedView(view, protected_view::check(path))`; то, что пишет, открывает наружу
  или выполняет (JavaScript 6.1, формы, вложения), проверяет `view->isProtectedView()`.

### Новая команда и клавиша (с 2.3–2.4)

1. Команда окна — `r.add({.id = …, .title = tr(…), .shortcuts = {…}, .run = …, .when = …, .enabled = …, .menu = …})` в
   `MainWindow::registerCommands()` (`src/ui/shell/menus.cpp`); команда инструмента — `Tool::commands()` (меню
   «Инструменты» и клавиши подхватятся сами). id `группа.действие` (`doc.save`, `view.zoomIn`), клавиши по умолчанию —
   **из таблицы эталона 6.8**; id команды с клавишей — строкой в `ACTIONS` `tools/hotkeys_guard.py` (иначе
   `unit.guard.hotkeys` красный). Будущий этап — `.stage = N` (серая с подсказкой «появится в версии 0.N»).
   Право документа или защищённый просмотр (2.5b) — `.blocked = …` (функция, непустая строка — причина: серая
   команда с подсказкой «Документ защищён: печать запрещена»; помощники `denied(Perm, текст)` и `protectedReason(текст)`
   в `registerCommands`); правка документа — ещё и `DocumentView::canEdit()`/`canAnnotate()` в `.enabled`.
   Команда правки — `.enabled = editable` (`DocumentView::canEdit()`: не «только просмотр» и не PDF/A в режиме чтения, 2.5).
2. Контекст: клавиша, которая мешала бы полям ввода (Home, стрелки, Ctrl+C), — `.when = "view"` (действует при фокусе в
   документе); режим (`readAloud.active`, `prepareForm`) — `.when = "<режим>"` + `commands().setContextActive(режим, true)`
   на время режима: его клавиша вытесняет такую же клавишу команды окна. Инструмент, которому клавиша вида нужна самому
   (Home/End в правке текста), — `PageView` принимает `QEvent::ShortcutOverride` для неё. Однобуквенные — `.singleKey =
   true` (действуют только с настройкой `hotkeys/singleKeyAccelerators`).
3. Конфликт клавиш — `CommandRegistry::conflicts()` (тест `ui.commands`) и подсветка в диалоге «Горячие клавиши» (9.2);
   переназначение — `hotkeys/<id>` в `settings.ini`.
4. Действие для рубежей — `ScriptRunner::registerAction(id, jsonSchema, fn)` (`--script`, 2.4b) с тем же id.
5. Палитра команд (`Ctrl+Shift+P`) берёт команды из реестра сама.

### Новое действие `--script` (с 2.4b)

Сценарий — JSON-массив шагов `{"cmd": "id", "args": {…}, "name": "…"}` (или `{"steps": [...]}`), запуск —
`AcroBatya.exe --script файл.json [--out каталог] [--data-dir каталог]` (без `--data-dir` — временный каталог данных,
оболочка — `FakeShellLauncher`); код 0 и «ok: N steps» в stdout, иначе код 1 и «step N cmd "name": …» в stderr.
Движок — `src/services/script_runner.h` (`ScriptRunner`, `ScriptContext`, `ScriptError`), базовые действия окна —
`src/ui/app/script_actions.cpp` (`doc.open/save/saveCopy/close/undo/redo/setProperty`, `view.zoom/zoomFit/zoomIn/
zoomOut/page/layout/rotate`, `command.run{id}`, `shot{name}`, `wait{ms}`). 4.6 — `ui/tools/certificate/sign_script.cpp`:
`sign.sign{pfx, password, level, reason, location, page, rect, field, lock, tsa, pkcs7, target}` (уровень по
умолчанию T; `rect` — пункты от левого верхнего угла, без него — невидимая), `sign.certify{pfx, password, perms,
level, reason, location, page, rect, tsa, target}`, `sign.timestamp{tsa, target}`, `sign.verifyAll{expect}` (valid,
certified, problems, invalid, notSigned), `sign.trust{cert, certified}`; «спросить» при отказе службы времени —
ошибка шага.

1. id — как у команды реестра (`группа.действие`: `edit.insertText`, `annot.create`, `form.setValue`, `sign.sign`,
   `ocr.recognize`); регистрирует сессия-владелец своей функцией `register<Имя>ScriptActions(ScriptRunner&,
   MainWindow&)` в папке своего инструмента (`src/ui/tools/<папка>/<имя>_script.cpp`) и строкой её вызова в конце
   `registerBaseActions` (`src/ui/app/script_actions.cpp`). Рубежи действий не изобретают.
2. Схема аргументов — JSON Schema строкой (`R"({"type": "object", "properties": {…}, "required": […],
   "additionalProperties": false})"`; понимаются `type`, `enum`, `minimum`/`maximum`, `minLength`, `properties`,
   `required`, `additionalProperties: false`, `items`, `minItems`/`maxItems`); страницы — с 1, как в интерфейсе.
   Неизвестный cmd и аргумент не по схеме ловятся до выполнения всего сценария.
3. Тело — только через `DocumentSession` и `services/*_ops.h` (как ui): `script::await(future, "id")` ждёт задачу с
   циклом событий, `script::document(window)` — текущий документ, `script::settle()` — после правки окна;
   пути — `context.path(arg)` (относительные — от файла сценария, `${VAR}` — окружение, `${OUT_DIR}`, `${SCRIPT_DIR}`);
   ошибка — `throw ScriptError("английский текст")`.
4. Тест — сценарий `tests/scripts/<имя>.json` и `runner.runFile` в `tests/ui/<имя>_test.cpp` (окно, `registerBaseActions`
   + своя регистрация); неверные аргументы — ошибка шага с его именем. Рубеж (`tools/verify_r<N>.py`) зовёт exe с
   `--out .queue/r<N>/…` и смотрит код и строку итога.

### Режим правки на странице (с 3.4)

1. Инструмент правки вида — наследник `ViewTool` в своей папке `ui/tools/<папка>/` со своим `ViewToolId` (3.4 —
   `Edit`, 4.6 — `Sign`: `SignPlacementTool`, `CertificateTool::placementOf`; новый слот — `kViewToolCount`, ветка в
   `QuickBar::sync`, сброс в `ToolController::documentChanged`/`pageChanged`); экземпляр ставит инструмент
   `ToolRegistry` при первом включении на вкладке:
   `view.tools().install(std::make_unique<…>(view, view.tools()))` (как `EditTool::modeOf`), включает —
   `setTool(id)`. Сброс при смене документа и правке страницы — `ViewTool::documentChanged`/`pageChanged`.
2. Набор текста: `capturesKeyboard()` — клавиши сначала инструменту (пробел и буквы не уходят «Руке» и
   однобуквенным ярлыкам), `wantsShortcut(key)` — эти клавиши раньше ярлыков окна (`ShortcutOverride`), IME —
   `acceptsInputMethod`/`inputMethodEvent`/`inputMethodQuery` (координаты вьюпорта) и
   `ToolController::inputMethodChanged()` при смене состояния.
3. Данные страницы — задачей (`services/edit_ops.h` `pageContent`: блоки с `ParagraphEditor` и картинки) с
   поколением страницы (правка страницы делает старые ответы устаревшими); правка — копией редактора, запись —
   `DocumentSession::editText` по одной за раз (новый источник — из ответа); картинки — `editing::*Image`.
4. Запрет правки документа — `editing::lock` (подпись, `/Perms`) и свойства вкладки (`canEdit`, права `Perm::Edit`):
   кнопки серые, причина — подсказкой и строкой панели.
5. Тест — `MainWindow` + `ToolRegistry::instance().activate("edit", doc)`, ожидание `hasContent(0)`; клавиши —
   `QTest::keyClicks(&view, …)` (латиница), кириллица — `QInputMethodEvent` с `commitString`; сцены —
   `ui/shots/edit_scenes.cpp` (`editScene(файл, then)` — после прихода блоков первой страницы).

### Модель контент-потока и блоки (с 3.1)

1. Всё — в потоке документа: `ContentModel::build(doc, page)` (значения, читаются из любого потока) →
   `blocksForPage(model)`; правка — `model.rebuildParagraph(block)` (заменяет операции показа) → `model.apply(doc)`
   (операция журнала, сжатие FlateDecode, `markHeavy`). Не тронутые операции — байт в байт (`serialize`).
2. Поведение — как у интерпретатора MuPDF (pdf-interpret.c, pdf-op-run.c, stext-device.c): лишние операнды не
   мешают, ошибка разбора сбрасывает операнды, `endstream` завершает страницу, Tw — только у однобайтового кода 32,
   символ, повторённый на том же месте ближе 0,1 кегля, fz_stext выбрасывает («ложный жирный»), выключенный слой
   `/OC` не показывается. Новое правило модели — сначала сверить с этими файлами MuPDF.
3. Пустая правка повторяет исходные сдвиги TJ числами, а Tm пишет только у оператора с начала строки: шум float в
   1e-4 pt переставляет глиф через границу субпикселя кеша MuPDF (рендер «не в порогах») и меняет добавленный
   пробел fz_stext на пороге 0,15 кегля.
4. Разбор чужого файла: `unit_tests --gtest_also_run_disabled_tests --gtest_filter=text_blocks.DISABLED_dump`
   (блоки и строки), `empty_edit.DISABLED_file` (пустая правка страницы), `empty_edit.DISABLED_op_at` (оператор у
   точки: исходные байты, Tm/Tlm, замена) — с `OTTISK_DUMP_FILE`, `OTTISK_DUMP_PAGE`, `OTTISK_DUMP_X/Y`.
5. Вспомогательные функции с общими именами (`unite`, `apply`…) в новом `.cpp` `ottisk_core` — в своём именованном
   пространстве, не в безымянном: unity-сборка склеивает файлы, и безымянные пространства соседей — одно.

### Правка абзаца (с 3.3)

1. Всё — в потоке документа: `ContentModel::build` → `blocksForPage` → `ParagraphEditor::fromBlock(model, block,
   blocks)` (блок «только чтение» — `std::invalid_argument`); правка редактора — без MuPDF в любом потоке
   (`insert/erase/replace/applyStyle`, `scriptChange` для индексов); запись — `DocumentSession::editText(editor,
   name)` (в ядре — `Rewriter(doc, FontLibrary, SystemFonts, FallbackFonts).rewrite(editor)`), раскладка без
   записи для курсора — `layoutText`. Результат — `RewriteReport` (`ok`, `error` Stale/ReadOnly/Signed/Failed,
   `bbox`, `dirty` — перерисовать, `overlaps` — наехал на блоки, `notices` — шрифты) и редактор с новым источником.
2. Редактор привязан к модели, по которой построен (`ParagraphSource::ops` + текст): после правки другого абзаца
   выше по потоку — Stale, редактор строится заново. Правка текста — одна операция журнала (`markHeavy`), undo —
   потоки байт в байт.
3. Своё в потоке: новый текст стоит на месте первого оператора абзаца внутри его BT (MCID сохраняются), состояние
   после — теми же операциями `StateRefs`; подчёркивание — `/OttU <<>> BDC … EMC` после ET (повторная правка
   снимает старое, `fromBlock` узнаёт). Новое правило записи — проверять `engine.edit_render` (вне абзаца —
   pixels == 0 на фикстурах и корпусе) и `compat.edit`.
4. Найти и заменить — `Replacer(doc, rewriter)`: `find` (предпросмотр, пропуски с причиной), `replaceOne`,
   `replacePage` (одна операция на страницу), `replaceAll` (прогресс и отмена между страницами).
5. Рендер вне правки: `diffPixelsOutside(before, after, {pixelRect(report.dirty, dpi, 2)})`. Разбор чужой страницы —
   `engine_tests --gtest_also_run_disabled_tests --gtest_filter=edit_render.DISABLED_file` с `OTTISK_DUMP_FILE` и
   `OTTISK_DUMP_PAGE`.

### Шрифт нового текста (с 3.2)

1. Шрифт пробега — `FontProbe::forPage(doc, page, resource)` (ресурс из `ContentModel::fonts()`); выбор для
   символа или строки — `FontResolver(lib, systemFonts, fallback).resolve/runs` (`FontLibrary` — одна на сеанс
   редактирования, `SystemFonts` — `ot::systemFonts()` из services или `FakeSystemFonts` в тестах, запасной —
   `editFallbackFonts(Settings)`). `FontChoice::newFont` — `FontEmbedder::add(page, choice.face, choice.embed)` →
   ресурс; коды — `FontEmbedder::encode` (новый) или `FontProbe::encode(u)->bytes` (исходный); синтетика —
   `syntheticOps` в текстовом объекте (Tr, w) и `syntheticTextMatrix` к Tm. Сообщения человеку — `notices()`
   (коды `fontNoticeCode`, текст — в ui).
2. Всё в одной операции журнала с записью потока: `FontEmbedder::add` внутри неё — вложенная. Undo снимает шрифт;
   `FontEmbedder` проверяет, жив ли объект, и встраивает заново (кеш ресурсов MuPDF `pdf_find_font_resource` не
   откатывается — `add` его забывает).
3. Сохранение — `SaveOptions::prepare = [&](Document&) { embedder.subset(); }` (полная запись): подмножества только
   своих шрифтов; после записи документ переоткрыт — номера объектов другие, урезанные шрифты не переиспользуются.
4. Внутри `mu::call` — ни `return`, ни объектов с деструкторами (`fztry_guard.py`): строки и карты готовить снаружи,
   внутри — только вызовы MuPDF и `const char*`.

### Печать (с 2.6)

1. Печать из своего места (контекстное меню, инструмент, сценарий) — `MainWindow::showPrintDialog(pages, printer)`
   (pages — «Выделенные страницы»; печать запрещена правами — nullptr) или без диалога — `MainWindow::printSilently(path,
   printer)`; свою раскладку не считать — `core/print/imposition.h` и `services/print_job.h` (`makePrintPlan`).
2. Новый параметр печати — поле `PrintSettings` (или `print::ImposeOptions`), его учёт в `impose`/`PrintJob`, поле в
   `PrintDialog::settings()`; настройка по умолчанию — ключ `print/` и `PrintPage`.
3. Тесты — никогда живой принтер: в unit — `FakePrintDevice`/`makePdfDevice` прямо в `PrintJob::run`, в ui —
   `ScopedPrintDevices` поверх `demoPrinters` (ui_main), в процессе exe — `OTTISK_FAKE_PRINTER`; GDI «в файл» — только
   `compat.print.pdf` (`print_compat.DISABLED_*`, «Microsoft Print to PDF»; нет — пропуск).

### Новый `OT_FEATURE`

1. ID — только из `docs/scope.json` (`full`/`partial`/`nice`) и пунктов плана; новый ID не выдумывается
   (`unit.guard.scope` красный).
2. В теле gtest-теста: `#include "core/util/features.h"` и `OT_FEATURE("view-10");` — пишет
   `RecordProperty("feature", id)` (в `build/<пресет>/gtest-xml/*.xml`) и строку `OT_FEATURE:view-10` в stdout;
   в QTest — только строку в stdout (`<system-out>` в `ctest.xml`).
3. Покрытие — `"$env:OTTISK_PYTHON" -X utf8 -I tools/scope_report.py` («закрыто N из M», к Р5 ≥ 95 % `full`).
4. В рабочем коде макрос не используется.

### Новый страж-скрипт

`tools/<имя>_guard.py` (stdlib + пакеты venv, `--root`), коды 0/1/77, нарушения — `файл:строка: что`;
регистрация `ottisk_guard(<имя> <имя>_guard.py)` в `CMakeLists.txt` (главный исполнитель) → ctest `unit.guard.<имя>`.

### Сетевой запрос программы (с 4.1)

1. Только через `net::HttpFetcher` (`core/util/http_fetcher.h`): в core — параметр-ссылка (как у `TsaClient`,
   `RevocationChecker`, `ChainBuilder::setFetcher`), в services/ui — `ot::httpFetcher()`; свой QNAM/WinHTTP — красный
   страж `unit.guard.boundaries`.
2. `Request.purpose`: `Signatures` — TSA, OCSP, CRL, AIA, списки доверия (работает при «Сеть: только подписи»),
   `General` — остальное; `sameHostRedirectsOnly = true` для TSA/OCSP, `false` — для CRL/AIA (CDN).
3. Ответ без исключений: `Status::Disabled` (выключатель) — честное «нет данных», не ошибка проверки.
4. Тесты — `FakeHttpFetcher` (ответы по адресу или обработчик; адреса тестовой PKI — `http://pki.acrobatya.test/…`)
   или живой `TsaServer` на 127.0.0.1; публичные адреса — только compat с `OTTISK_LIVE_NET=1`.

### Тест с подписью, PKI и TSA (с 4.1)

Удостоверение — `loadPkcs12(readPki("signer-rsa.pfx"), "acrobatya")` (ECDSA — `signer-ec.pfx`, legacy, «пароль»);
якоря — `ot::test::pkiRootStore()`; метка времени без Python — `FakeHttpFetcher` с обработчиком `localTsaReply(r.body)`,
живая — `TsaServer::start`. Независимая проверка — `opensslExe()` + `runProcess` (`cms -verify -cades -binary -inform DER
-content … -CAfile root.pem -purpose smimesign -out <файл>`; `ts -verify -token_in -digest HEX`), нет openssl — `GTEST_SKIP`.
Новая PKI — `tools/pki/make_test_pki.ps1` (ключи меняются — пересобрать фикстуры, которые на них подписаны);
OCSP-ответы отдельно — `make_ocsp_fixtures.ps1`.

### Подпись документа (с 4.2)

1. Подписать — `sign::signDocument(doc, SignRequest, target)` (в окне — `DocumentSession::sign`): удостоверение — `IdentityStore::open(id, пароль)` (`NeedPassword` — спросить; запись `WindowsStore` — `WindowsCert{record.thumbprint, hwnd окна}`, выбор нового — `CertPicker::pick`, ключ CNG, PIN — окно Windows), вид — `SignatureAppearance` (ярлыки — переводы `tr()` в `AppearanceLabels`), метка B-T — `defaultTsaClient(Settings)` и `withoutTimestamp` по `timestampFallback`. Свой подписант или своё поле (DocTimeStamp, DSS) — `detail::FieldJob` + `signField` (`sign_field.h`): правки после `pdf_sign_signature_with_appearance` — только в `patch` (секция подписи), каталог — в `before`.
2. Тесты — `support/sign_support.h` (`pkiIdentity`, `windowsIdentity` — тот же .pfx как сертификат «хранилища» через PFX-импорт в памяти, без окон; `localTsa`, `unsignedCopy`, `makeFormFile`, `readSignatures` → `DocView`, `expectCmsValid`, `coversFile`; в `engine_tests` те же помощники подключены `target_sources`); метка — `FakeHttpFetcher` + `localTsaReply` (TsaClient держит fetcher ссылкой — держать оба живыми); внешняя проверка — `sign_compat.DISABLED_*` в `OTTISK_COMPAT_OUT/sign/<набор>` и `compat.sign.*` (`compat_check.py --trust root.pem --expect-sig …`, метки документа — `--expect-ts`; токены `TIMESTAMPED`, `TIMESTAMP_TRUSTED`, `DOC_TIMESTAMP`). Вид — эталоны `tests/fixtures/sig/appearance/*.png` (`OTTISK_UPDATE_SIG_APPEARANCE=1`).

### Сведения LTV, уровни, сертификация и списки доверия (с 4.5)

1. «Добавить сведения для проверки» — `sign::addValidationInfo(doc, LtvOptions{trust, fetcher, vri})` (в окне —
   `DocumentSession::addValidationInfo`): материал собирает `collectLtv` (цепочки до якоря или самоподписанного корня,
   отзыв через `RevocationChecker` — сначала данные файла), пишет `writeDss` одной операцией журнала, `Saver::Incremental`;
   нового нет — без записи. «LTV включено» в интерфейсе — `ltvEnabled(doc, sig, {trust})` (сеть не нужна); при
   сохранении — `decideAutoLtv(ltvAutoAddFromName(settings kAutoAddLtv), размер файла, collected.added().bytes())`.
2. Уровень подписи — `SignRequest::level` (LT/LTA — нужны `timestamp`, `trust`, `revocationFetcher`); продление архивной
   метки — `extendArchiveTimestamp` (`DocumentSession::extendTimestamp`); сертификация — `SignRequest::certify` (до
   подписи интерфейс зовёт `certifyBlock`), что можно делать с сертифицированным — `mdpAllows(readDocMdp(doc).permission,
   MdpAction::…)`; «Проверить документ» перед подписью — `scanWarnings(doc)`.
3. Списки доверия — `TrustUpdater` (раз в 14 дней; ожидающее — `pending()`, согласие — `install()`); вручную —
   `downloadTrustLists` + `installTrustLists`; новые pins LOTL — `tools/gen/make_trust_fixtures.py` по свежему снимку
   (человек) и `compat.trust.live`. Доверие из подписи — `trustFromSignature(store, sig, flags[, отпечаток])`, права
   сертифицированного документа (JavaScript, динамический контент — 6.1) — `certifiedPrivileges(verification, store)`.
4. Тесты — `support/verify_support.h` и `sign_support.h`; сеть PKI — `pkiResponder()` (OCSP signer-rsa/ec, CRL ЦС), служба
   времени — `localTsa()`; внешняя проверка — `compat_check.py --expect-sig …,LTV[,LTA]` (pyHanko по данным /DSS без
   сети с обязательным отзывом; LTA — `ades_lta_validation`); списки — `tests/fixtures/trust/` и `FakeHttpFetcher` по
   адресам `kLotlUrl` и TL из `expected.json`.

### Проверка подписей (с 4.4)

1. Проверить — `sign::verifyDocument(doc, VerifyOptions)` (поток документа): якоря — `TrustStore::load(<данные>/trust)`
   (в окне — `CryptoWorker` + `VerifyRequest::fromSettings`, якоря — снимок `TrustService`, 4.6), сеть —
   `ot::httpFetcher()` при разрешённой сети, иначе `nullptr` (отзыв по данным файла); итог —
   `DocumentVerification::status` (полоса) и `SignatureInfo::verdict` + `findings` (панель «Подписи», «Подробнее» —
   `Finding::detail` на английском). Новая причина — код `Issue` с `issueName`/`issueSeverity` и строкой `SignTexts::issue` (4.6).
2. Тесты — `support/verify_support.h`: свои подписи PKI, порча DER `/Contents` (`patchContents`) и байтов (`patchBytes`),
   сеть PKI (`pkiResponder`) или «нет сети» (`offlineFetcher`), сертификация P=1/2/3 до 4.5 — `certify` через
   `detail::FieldJob`, правки после подписи — `editIncrementally` + `setTextField`/`addTextNote`; файлы EU DSS — `dssFile`
   (нет корпуса — SKIP), ожидания — `verifier_expected.json`. Новое расхождение на корпусе DSS — разобрать причину
   pyHanko (`tools/compare_pyhanko.py`), записать «manual» с кодом в `dss_expectations.json`, легенда — в `about`.

### Интерфейс подписей (с 4.6)

1. Подписать, сертифицировать, поставить метку из окна — `CertificateTool` (`ToolRegistry::instance()`, id
   `certificate`): `startSign`/`startCertify(visible)`/`startTimestamp` → рамка `SignPlacementTool`
   (`placementOf(view)`; невидимая — без рамки) → `placeSignature(page, rect, field)` →
   `SignDialog`/`CertifyDialog`/`TimestampDialog` → «Сохранить как» → `DocumentSession::sign`/`timestampDocument`;
   «подписать без метки?» из потока документа — `Qt::BlockingQueuedConnection`; после B-B/B-T —
   `signing::ltvDecision` → `offerLtv`. Причина запрета — `blockReason(Action)`; строка о подписях (ошибка, статус,
   вердикт, `Issue`, предупреждение) — функция `SignTexts`.
2. Задачи над подписями — `services/sign_ops.h` (`signing::fields`, `warnings`, `certifyBlock`, `docMdp`,
   `clearBlock`/`clearSignature`, `signedVersion`, `ltvDecision`); хранилище доверия в ui и службах — только
   `TrustService::shared()`: чтение — `snapshot()`, правка — `edit([](sign::TrustStore& s) { … })` (anchors.json,
   сигнал `changed` — вкладки проверяются заново); свой `TrustStore::load` мимо службы — снимки разойдутся.
3. Проверяется записанный файл: `view.signatures()` (`SignatureStatus::verify` →
   `CryptoWorker::shared().verify(session, VerifyRequest::fromSettings(settings))`, пул из двух потоков); повтор после
   `DocumentSession::saved` и `TrustService::changed` — сам, по поколению. Полоса, значки, чип строки состояния и
   панель «Подписи» читают итог (`verified`, `verification()`, `signature(field)`, `fields()`) — свой вызов
   валидатора в ui не нужен.
4. Новая настройка подписей — ключ `signatures/` (рецепт «Новая настройка») и поле вкладки `SignaturesPage`
   (`tabIds()`, `showTab(id)`); удостоверения, якоря и корни Windows — сразу (`IdentitiesWidget`, `TrustedWidget`),
   остальное — по «ОК»; `TrustUpdater` читает `Settings` — перед фоновой работой `apply` + `sync`.
5. Тесты — `tests/ui/support/sign_ui.h` (`PkiNetwork`, `signFile`, `tamperFile`, `trustPkiRoot`, `verifyFile`);
   модальное — `setFileChooser`/`setPasswordPrompt`/`setTimestampPrompt`/`setTrustPrompt`/`setConfirm`; итог проверки
   приходит сигналом из пула — ждать `hasResult() && !isVerifying()` (`script::await` не подходит: `sign.verifyAll`
   крутит `processEvents` до 120 с); уровень важен — `signatures/autoAddLtv=never`. Сцены — `src/ui/shots/sign_scenes.cpp`.

### Новая фикстура

Генератор — `tools/gen/<что>.py` (`"$env:OTTISK_PYTHON" -X utf8 -I`, детерминированный: фиксированные `/ID`,
даты, seed — два прогона дают одинаковые sha256) или `.ps1` (Word COM, UTF-8 BOM); результат ≤ 50 КБ — в
`tests/fixtures/<тема>/`, больше — не в Git (генерируется заново).

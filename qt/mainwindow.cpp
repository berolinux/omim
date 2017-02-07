#include "qt/about.hpp"
#include "qt/draw_widget.hpp"
#include "qt/mainwindow.hpp"
#include "qt/osm_auth_dialog.hpp"
#include "qt/preferences_dialog.hpp"
#include "qt/search_panel.hpp"
#include "qt/slider_ctrl.hpp"
#include "qt/traffic_mode.hpp"
#include "qt/traffic_panel.hpp"
#include "qt/trafficmodeinitdlg.h"

#include "openlr/openlr_sample.hpp"

#include "platform/settings.hpp"
#include "platform/platform.hpp"

#include "defines.hpp"

#include <sstream>

#include "std/target_os.hpp"

#include "build_style/build_style.h"
#include "build_style/build_statistics.h"
#include "build_style/run_tests.h"

#include <QtGui/QCloseEvent>
#include <QFileDialog>

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
  #include <QtGui/QAction>
  #include <QtGui/QDesktopWidget>
  #include <QtGui/QDockWidget>
  #include <QtGui/QMenu>
  #include <QtGui/QMenuBar>
  #include <QtGui/QToolBar>
  #include <QtGui/QPushButton>
  #include <QtGui/QHBoxLayout>
  #include <QtGui/QLabel>
#else
  #include <QtWidgets/QAction>
  #include <QtWidgets/QDesktopWidget>
  #include <QtWidgets/QDockWidget>
  #include <QtWidgets/QMenu>
  #include <QtWidgets/QMenuBar>
  #include <QtWidgets/QToolBar>
  #include <QtWidgets/QPushButton>
  #include <QtWidgets/QHBoxLayout>
  #include <QtWidgets/QLabel>
#endif

#include <QMessageBox>

#define IDM_ABOUT_DIALOG        1001
#define IDM_PREFERENCES_DIALOG  1002

#ifndef NO_DOWNLOADER
#include "qt/update_dialog.hpp"
#include "qt/info_dialog.hpp"

#include "indexer/classificator.hpp"

#include <QtCore/QFile>

#endif // NO_DOWNLOADER

#ifdef BUILD_DESIGNER
#include "drape/debug_rect_renderer.hpp"
#endif // BUILD_DESIGNER

namespace
{
// TODO(mgsergio): Consider getting rid of this class: just put everything
// in TrafficMode.
class TrafficDrawerDelegate : public TrafficDrawerDelegateBase
{
public:
  explicit TrafficDrawerDelegate(qt::DrawWidget & drawWidget)
    : m_framework(drawWidget.GetFramework())
    , m_drapeApi(m_framework.GetDrapeApi())
  {
  }

  void SetViewportCenter(m2::PointD const & center) override
  {
    m_framework.SetViewportCenter(center);
  }

  void DrawDecodedSegments(DecodedSample const & sample, int const sampleIndex) override
  {
    CHECK(!sample.GetItems().empty(), ("Sample must not be empty."));
    auto const & points = sample.GetPoints(sampleIndex);

    LOG(LINFO, ("Decoded segment", points));
    m_drapeApi.AddLine(NextLineId(),
                       df::DrapeApiLineData(points, dp::Color(0, 0, 255, 255))
                       .Width(3.0f).ShowPoints(true /* markPoints */));
  }

  void DrawEncodedSegment(openlr::LinearSegment const & segment) override
  {
    auto const & points = segment.GetMercatorPoints();

    LOG(LINFO, ("Encoded segment", points));
    m_drapeApi.AddLine(NextLineId(),
                       df::DrapeApiLineData(points, dp::Color(255, 0, 0, 255))
                       .Width(3.0f).ShowPoints(true /* markPoints */));
  }

  void Clear() override
  {
    m_drapeApi.Clear();
  }

private:
  string NextLineId() { return strings::to_string(m_lineId++); }

  uint32_t m_lineId = 0;

  Framework & m_framework;
  df::DrapeApi & m_drapeApi;
};
}  // namespace

namespace qt
{

// Defined in osm_auth_dialog.cpp.
extern char const * kTokenKeySetting;
extern char const * kTokenSecretSetting;

MainWindow::MainWindow(QString const & mapcssFilePath /*= QString()*/)
  : m_Docks{}
  , m_pBuildStyleAction(nullptr)
  , m_pDrawDebugRectAction(nullptr)
  , m_pGetStatisticsAction(nullptr)
  , m_pRunTestsAction(nullptr)
  , m_locationService(CreateDesktopLocationService(*this))
  , m_mapcssFilePath(mapcssFilePath)
{
  // Always runs on the first desktop
  QDesktopWidget const * desktop(QApplication::desktop());
  setGeometry(desktop->screenGeometry(desktop->primaryScreen()));

  m_pDrawWidget = new DrawWidget(this);
  QSurfaceFormat format = m_pDrawWidget->format();

  format.setMajorVersion(2);
  format.setMinorVersion(1);

  format.setAlphaBufferSize(8);
  format.setBlueBufferSize(8);
  format.setGreenBufferSize(8);
  format.setRedBufferSize(8);
  format.setStencilBufferSize(0);
  format.setSamples(0);
  format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  format.setSwapInterval(1);
  format.setDepthBufferSize(16);

  format.setProfile(QSurfaceFormat::CompatibilityProfile);
  //format.setOption(QSurfaceFormat::DebugContext);
  m_pDrawWidget->setFormat(format);
  m_pDrawWidget->setMouseTracking(true);
  setCentralWidget(m_pDrawWidget);

  QObject::connect(m_pDrawWidget, SIGNAL(BeforeEngineCreation()), this, SLOT(OnBeforeEngineCreation()));

  CreateCountryStatusControls();
  CreateNavigationBar();
  CreateSearchBarAndPanel();

  QString caption = qAppName();
  if (!m_mapcssFilePath.isEmpty())
    caption += QString(" - ") + m_mapcssFilePath;

  setWindowTitle(caption);
  setWindowIcon(QIcon(":/ui/logo.png"));

  QMenu * trafficMarkup = new QMenu(tr("Traffic"), this);
  menuBar()->addMenu(trafficMarkup);
  trafficMarkup->addAction(tr("Open sample"), this, SLOT(OnOpenTrafficSample()));
  m_saveTrafficSampleAction = trafficMarkup->addAction(tr("Save sample"), this,
                                                       SLOT(OnSaveTrafficSample()));
  m_saveTrafficSampleAction->setEnabled(false);

  m_quitTrafficModeAction = new QAction(tr("Quit traffic mode"), this);
  // On macOS actions with names started with quit or exit are treated specially,
  // see QMenuBar documentation.
  m_quitTrafficModeAction->setMenuRole(QAction::MenuRole::NoRole);
  m_quitTrafficModeAction->setEnabled(false);
  connect(m_quitTrafficModeAction, SIGNAL(triggered()), this, SLOT(OnQuitTrafficMode()));
  trafficMarkup->addAction(m_quitTrafficModeAction);

#ifndef OMIM_OS_WINDOWS
  QMenu * helpMenu = new QMenu(tr("Help"), this);
  menuBar()->addMenu(helpMenu);
  helpMenu->addAction(tr("About"), this, SLOT(OnAbout()));
  helpMenu->addAction(tr("Preferences"), this, SLOT(OnPreferences()));
  helpMenu->addAction(tr("OpenStreetMap Login"), this, SLOT(OnLoginMenuItem()));
  helpMenu->addAction(tr("Upload Edits"), this, SLOT(OnUploadEditsMenuItem()));
#else
  {
    // create items in the system menu
    HMENU menu = ::GetSystemMenu((HWND)winId(), FALSE);
    MENUITEMINFOA item;
    item.cbSize = sizeof(MENUITEMINFOA);
    item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING;
    item.fType = MFT_STRING;
    item.wID = IDM_PREFERENCES_DIALOG;
    QByteArray const prefsStr = tr("Preferences...").toLocal8Bit();
    item.dwTypeData = const_cast<char *>(prefsStr.data());
    item.cch = prefsStr.size();
    ::InsertMenuItemA(menu, ::GetMenuItemCount(menu) - 1, TRUE, &item);
    item.wID = IDM_ABOUT_DIALOG;
    QByteArray const aboutStr = tr("About...").toLocal8Bit();
    item.dwTypeData = const_cast<char *>(aboutStr.data());
    item.cch = aboutStr.size();
    ::InsertMenuItemA(menu, ::GetMenuItemCount(menu) - 1, TRUE, &item);
    item.fType = MFT_SEPARATOR;
    ::InsertMenuItemA(menu, ::GetMenuItemCount(menu) - 1, TRUE, &item);
  }
#endif

  // Always show on full screen.
  showMaximized();

#ifndef NO_DOWNLOADER
  // Show intro dialog if necessary
  bool bShow = true;
  (void)settings::Get("ShowWelcome", bShow);

  if (bShow)
  {
    bool bShowUpdateDialog = true;

    string text;
    try
    {
      ReaderPtr<Reader> reader = GetPlatform().GetReader("welcome.html");
      reader.ReadAsString(text);
    }
    catch (...)
    {}

    if (!text.empty())
    {
      InfoDialog welcomeDlg(QString("Welcome to ") + qAppName(), text.c_str(),
                            this, QStringList(tr("Download Maps")));
      if (welcomeDlg.exec() == QDialog::Rejected)
        bShowUpdateDialog = false;
    }
    settings::Set("ShowWelcome", false);

    if (bShowUpdateDialog)
      ShowUpdateDialog();
  }
#endif // NO_DOWNLOADER

  m_pDrawWidget->UpdateAfterSettingsChanged();
}

#if defined(Q_WS_WIN)
bool MainWindow::winEvent(MSG * msg, long * result)
{
  if (msg->message == WM_SYSCOMMAND)
  {
    switch (msg->wParam)
    {
    case IDM_PREFERENCES_DIALOG:
      OnPreferences();
      *result = 0;
      return true;
    case IDM_ABOUT_DIALOG:
      OnAbout();
      *result = 0;
      return true;
    }
  }
  return false;
}
#endif

void MainWindow::LocationStateModeChanged(location::EMyPositionMode mode)
{
  if (mode == location::PendingPosition)
  {
    m_locationService->Start();
    m_pMyPositionAction->setIcon(QIcon(":/navig64/location-search.png"));
    m_pMyPositionAction->setToolTip(tr("Looking for position..."));
    return;
  }

  m_pMyPositionAction->setIcon(QIcon(":/navig64/location.png"));
  m_pMyPositionAction->setToolTip(tr("My Position"));
}

namespace
{
  struct button_t
  {
    QString name;
    char const * icon;
    char const * slot;
  };

  void add_buttons(QToolBar * pBar, button_t buttons[], size_t count, QObject * pReceiver)
  {
    for (size_t i = 0; i < count; ++i)
    {
      if (buttons[i].icon)
        pBar->addAction(QIcon(buttons[i].icon), buttons[i].name, pReceiver, buttons[i].slot);
      else
        pBar->addSeparator();
    }
  }

  struct hotkey_t
  {
    int key;
    char const * slot;
  };

  void FormatMapSize(uint64_t sizeInBytes, string & units, size_t & sizeToDownload)
  {
    int const mbInBytes = 1024 * 1024;
    int const kbInBytes = 1024;
    if (sizeInBytes > mbInBytes)
    {
      sizeToDownload = (sizeInBytes + mbInBytes - 1) / mbInBytes;
      units = "MB";
    }
    else if (sizeInBytes > kbInBytes)
    {
      sizeToDownload = (sizeInBytes + kbInBytes -1) / kbInBytes;
      units = "KB";
    }
    else
    {
      sizeToDownload = sizeInBytes;
      units = "B";
    }
  }
}

void MainWindow::CreateNavigationBar()
{
  QToolBar * pToolBar = new QToolBar(tr("Navigation Bar"), this);
  pToolBar->setOrientation(Qt::Vertical);
  pToolBar->setIconSize(QSize(32, 32));

  {
    // Add navigation hot keys.
    hotkey_t const arr[] = {
      { Qt::Key_Equal, SLOT(ScalePlus()) },
      { Qt::Key_Minus, SLOT(ScaleMinus()) },
      { Qt::ALT + Qt::Key_Equal, SLOT(ScalePlusLight()) },
      { Qt::ALT + Qt::Key_Minus, SLOT(ScaleMinusLight()) },
      { Qt::Key_A, SLOT(ShowAll()) },
      // Use CMD+n (New Item hotkey) to activate Create Feature mode.
      { Qt::Key_Escape, SLOT(ChoosePositionModeDisable()) }
    };

    for (size_t i = 0; i < ARRAY_SIZE(arr); ++i)
    {
      QAction * pAct = new QAction(this);
      pAct->setShortcut(QKeySequence(arr[i].key));
      connect(pAct, SIGNAL(triggered()), m_pDrawWidget, arr[i].slot);
      addAction(pAct);
    }
  }

  {
    m_trafficEnableAction = pToolBar->addAction(QIcon(":/navig64/traffic.png"), tr("Show traffic"),
                                                this, SLOT(OnTrafficEnabled()));
    m_trafficEnableAction->setCheckable(true);
    m_trafficEnableAction->setChecked(m_pDrawWidget->GetFramework().LoadTrafficEnabled());
    pToolBar->addSeparator();

    // TODO(AlexZ): Replace icon.
    m_pCreateFeatureAction = pToolBar->addAction(QIcon(":/navig64/select.png"), tr("Create Feature"),
                                                 this, SLOT(OnCreateFeatureClicked()));
    m_pCreateFeatureAction->setCheckable(true);
    m_pCreateFeatureAction->setToolTip(tr("Please select position on a map."));
    m_pCreateFeatureAction->setShortcut(QKeySequence::New);

    pToolBar->addSeparator();

    m_selectionMode = pToolBar->addAction(QIcon(":/navig64/selectmode.png"), tr("Selection mode"),
                                          this, SLOT(OnSwitchSelectionMode()));
    m_selectionMode->setCheckable(true);
    m_selectionMode->setToolTip(tr("Turn on/off selection mode"));

    m_clearSelection = pToolBar->addAction(QIcon(":/navig64/clear.png"), tr("Clear selection"),
                                           this, SLOT(OnClearSelection()));
    m_clearSelection->setToolTip(tr("Clear selection"));

    pToolBar->addSeparator();

    // Add search button with "checked" behavior.
    m_pSearchAction = pToolBar->addAction(QIcon(":/navig64/search.png"), tr("Search"),
                                          this, SLOT(OnSearchButtonClicked()));
    m_pSearchAction->setCheckable(true);
    m_pSearchAction->setToolTip(tr("Offline Search"));
    m_pSearchAction->setShortcut(QKeySequence::Find);

    pToolBar->addSeparator();

// #ifndef OMIM_OS_LINUX
    // add my position button with "checked" behavior

    m_pMyPositionAction = pToolBar->addAction(QIcon(":/navig64/location.png"),
                                           tr("My Position"),
                                           this,
                                           SLOT(OnMyPosition()));
    m_pMyPositionAction->setCheckable(true);
    m_pMyPositionAction->setToolTip(tr("My Position"));
// #endif

#ifdef BUILD_DESIGNER
    // Add "Build style" button
    if (!m_mapcssFilePath.isEmpty())
    {
      m_pBuildStyleAction = pToolBar->addAction(QIcon(":/navig64/run.png"),
                                                tr("Build style"),
                                                this,
                                                SLOT(OnBuildStyle()));
      m_pBuildStyleAction->setCheckable(false);
      m_pBuildStyleAction->setToolTip(tr("Build style"));

      m_pRecalculateGeomIndex = pToolBar->addAction(QIcon(":/navig64/geom.png"),
                                                    tr("Recalculate geometry index"),
                                                    this,
                                                    SLOT(OnRecalculateGeomIndex()));
      m_pRecalculateGeomIndex->setCheckable(false);
      m_pRecalculateGeomIndex->setToolTip(tr("Recalculate geometry index"));
    }

    // Add "Debug style" button
    m_pDrawDebugRectAction = pToolBar->addAction(QIcon(":/navig64/bug.png"),
                                              tr("Debug style"),
                                              this,
                                              SLOT(OnDebugStyle()));
    m_pDrawDebugRectAction->setCheckable(true);
    m_pDrawDebugRectAction->setChecked(false);
    m_pDrawDebugRectAction->setToolTip(tr("Debug style"));
    dp::DebugRectRenderer::Instance().SetEnabled(false);

    // Add "Get statistics" button
    m_pGetStatisticsAction = pToolBar->addAction(QIcon(":/navig64/chart.png"),
                                                 tr("Get statistics"),
                                                 this,
                                                 SLOT(OnGetStatistics()));
    m_pGetStatisticsAction->setCheckable(false);
    m_pGetStatisticsAction->setToolTip(tr("Get statistics"));

    // Add "Run tests" button
    m_pRunTestsAction = pToolBar->addAction(QIcon(":/navig64/test.png"),
                                            tr("Run tests"),
                                            this,
                                            SLOT(OnRunTests()));
    m_pRunTestsAction->setCheckable(false);
    m_pRunTestsAction->setToolTip(tr("Run tests"));

#endif // BUILD_DESIGNER

    // add view actions 1
    button_t arr[] = {
      { QString(), 0, 0 },
      //{ tr("Show all"), ":/navig64/world.png", SLOT(ShowAll()) },
      { tr("Scale +"), ":/navig64/plus.png", SLOT(ScalePlus()) }
    };
    add_buttons(pToolBar, arr, ARRAY_SIZE(arr), m_pDrawWidget);
  }

  // add scale slider
  QScaleSlider * pScale = new QScaleSlider(Qt::Vertical, this, 20);
  pScale->SetRange(2, scales::GetUpperScale());
  pScale->setTickPosition(QSlider::TicksRight);

  pToolBar->addWidget(pScale);
  m_pDrawWidget->SetScaleControl(pScale);

  {
    // add view actions 2
    button_t arr[] = {
      { tr("Scale -"), ":/navig64/minus.png", SLOT(ScaleMinus()) }
    };
    add_buttons(pToolBar, arr, ARRAY_SIZE(arr), m_pDrawWidget);
  }

#ifndef NO_DOWNLOADER
  {
    // add mainframe actions
    button_t arr[] = {
      { QString(), 0, 0 },
      { tr("Download Maps"), ":/navig64/download.png", SLOT(ShowUpdateDialog()) }
    };
    add_buttons(pToolBar, arr, ARRAY_SIZE(arr), this);
  }
#endif // NO_DOWNLOADER

  addToolBar(Qt::RightToolBarArea, pToolBar);
}

void MainWindow::CreateCountryStatusControls()
{
  QHBoxLayout * mainLayout = new QHBoxLayout();

  m_downloadButton = new QPushButton("Download");
  mainLayout->addWidget(m_downloadButton, 0, Qt::AlignHCenter);
  m_downloadButton->setVisible(false);
  connect(m_downloadButton, SIGNAL(released()), this, SLOT(OnDownloadClicked()));

  m_retryButton = new QPushButton("Retry downloading");
  mainLayout->addWidget(m_retryButton, 0, Qt::AlignHCenter);
  m_retryButton->setVisible(false);
  connect(m_retryButton, SIGNAL(released()), this, SLOT(OnRetryDownloadClicked()));

  m_downloadingStatusLabel = new QLabel("Downloading");
  mainLayout->addWidget(m_downloadingStatusLabel, 0, Qt::AlignHCenter);
  m_downloadingStatusLabel->setVisible(false);

  m_pDrawWidget->setLayout(mainLayout);

  m_pDrawWidget->SetCurrentCountryChangedListener([this](storage::TCountryId const & countryId,
                                                         string const & countryName, storage::Status status,
                                                         uint64_t sizeInBytes, uint8_t progress)
  {
    m_lastCountry = countryId;
    if (m_lastCountry.empty() || status == storage::Status::EOnDisk || status == storage::Status::EOnDiskOutOfDate)
    {
      m_downloadButton->setVisible(false);
      m_retryButton->setVisible(false);
      m_downloadingStatusLabel->setVisible(false);
    }
    else
    {
      if (status == storage::Status::ENotDownloaded)
      {
        m_downloadButton->setVisible(true);
        m_retryButton->setVisible(false);
        m_downloadingStatusLabel->setVisible(false);

        string units;
        size_t sizeToDownload = 0;
        FormatMapSize(sizeInBytes, units, sizeToDownload);
        std::stringstream str;
        str << "Download (" << countryName << ") " << sizeToDownload << units;
        m_downloadButton->setText(str.str().c_str());
      }
      else if (status == storage::Status::EDownloading)
      {
        m_downloadButton->setVisible(false);
        m_retryButton->setVisible(false);
        m_downloadingStatusLabel->setVisible(true);

        std::stringstream str;
        str << "Downloading (" << countryName << ") " << (int)progress << "%";
        m_downloadingStatusLabel->setText(str.str().c_str());
      }
      else if (status == storage::Status::EInQueue)
      {
        m_downloadButton->setVisible(false);
        m_retryButton->setVisible(false);
        m_downloadingStatusLabel->setVisible(true);

        std::stringstream str;
        str << countryName << " is waiting for downloading";
        m_downloadingStatusLabel->setText(str.str().c_str());
      }
      else
      {
        m_downloadButton->setVisible(false);
        m_retryButton->setVisible(true);
        m_downloadingStatusLabel->setVisible(false);

        std::stringstream str;
        str << "Retry to download " << countryName;
        m_retryButton->setText(str.str().c_str());
      }
    }
  });
}

void MainWindow::OnAbout()
{
  AboutDialog dlg(this);
  dlg.exec();
}

void MainWindow::OnLocationError(location::TLocationError errorCode)
{
  switch (errorCode)
  {
  case location::EDenied:
    m_pMyPositionAction->setEnabled(false);
    break;

  default:
    ASSERT(false, ("Not handled location notification:", errorCode));
    break;
  }

  m_pDrawWidget->GetFramework().OnLocationError(errorCode);
}

void MainWindow::OnLocationUpdated(location::GpsInfo const & info)
{
  m_pDrawWidget->GetFramework().OnLocationUpdate(info);
}

void MainWindow::OnMyPosition()
{
  if (m_pMyPositionAction->isEnabled())
    m_pDrawWidget->GetFramework().SwitchMyPositionNextMode();
}

void MainWindow::OnCreateFeatureClicked()
{
  if (m_pCreateFeatureAction->isChecked())
  {
    m_pDrawWidget->ChoosePositionModeEnable();
  }
  else
  {
    m_pDrawWidget->ChoosePositionModeDisable();
    m_pDrawWidget->CreateFeature();
  }
}

void MainWindow::OnSwitchSelectionMode()
{
  m_pDrawWidget->SetSelectionMode(m_selectionMode->isChecked());
}

void MainWindow::OnClearSelection() { m_pDrawWidget->GetFramework().GetDrapeApi().Clear(); }
void MainWindow::OnSearchButtonClicked()
{
  if (m_pSearchAction->isChecked())
    m_Docks[0]->show();
  else
    m_Docks[0]->hide();
}

void MainWindow::OnLoginMenuItem()
{
  OsmAuthDialog dlg(this);
  dlg.exec();
}

void MainWindow::OnUploadEditsMenuItem()
{
  string key, secret;
  settings::Get(kTokenKeySetting, key);
  settings::Get(kTokenSecretSetting, secret);
  if (key.empty() || secret.empty())
    OnLoginMenuItem();
  else
  {
    auto & editor = osm::Editor::Instance();
    if (editor.HaveMapEditsOrNotesToUpload())
      editor.UploadChanges(key, secret, {{"created_by", "MAPS.ME " OMIM_OS_NAME}});
  }
}

void MainWindow::OnBeforeEngineCreation()
{
  m_pDrawWidget->GetFramework().SetMyPositionModeListener([this](location::EMyPositionMode mode, bool routingActive)
  {
    LocationStateModeChanged(mode);
  });
}

void MainWindow::OnPreferences()
{
  PreferencesDialog dlg(this);
  dlg.exec();

  m_pDrawWidget->GetFramework().SetupMeasurementSystem();
  m_pDrawWidget->GetFramework().EnterForeground();
}

#ifdef BUILD_DESIGNER
void MainWindow::OnBuildStyle()
{
  try
  {
    build_style::BuildAndApply(m_mapcssFilePath);
    // m_pDrawWidget->RefreshDrawingRules();

    bool enabled = false;
    settings::Get(kEnabledAutoRegenGeomIndex, enabled);
    if (enabled)
    {
      build_style::NeedRecalculate = true;
      QMainWindow::close();
    }
  }
  catch (std::exception & e)
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Error");
    msgBox.setText(e.what());
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
  }
}

void MainWindow::OnRecalculateGeomIndex()
{
  try
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Warning");
    msgBox.setText("Geometry index will be regenerated. It can take a while.\nApplication may be closed and reopened!");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);
    if (msgBox.exec() == QMessageBox::Yes)
    {
      build_style::NeedRecalculate = true;
      QMainWindow::close();
    }
  }
  catch (std::exception & e)
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Error");
    msgBox.setText(e.what());
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
  }
}

void MainWindow::OnDebugStyle()
{
  bool const checked = m_pDrawDebugRectAction->isChecked();
  dp::DebugRectRenderer::Instance().SetEnabled(checked);
  m_pDrawWidget->RefreshDrawingRules();
}

void MainWindow::OnGetStatistics()
{
  try
  {
    QString text = build_style::GetCurrentStyleStatistics();
    InfoDialog dlg(QString("Style statistics"), text, NULL);
    dlg.exec();
  }
  catch (std::exception & e)
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Error");
    msgBox.setText(e.what());
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
  }
}

void MainWindow::OnRunTests()
{
  try
  {
    pair<bool, QString> res = build_style::RunCurrentStyleTests();
    InfoDialog dlg(QString("Style tests: ") + (res.first ? "OK" : "FAILED"), res.second, NULL);
    dlg.exec();
  }
  catch (std::exception & e)
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Error");
    msgBox.setText(e.what());
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
  }
}
#endif // BUILD_DESIGNER

#ifndef NO_DOWNLOADER
void MainWindow::ShowUpdateDialog()
{
  UpdateDialog dlg(this, m_pDrawWidget->GetFramework());
  dlg.ShowModal();
  m_pDrawWidget->update();
}

#endif // NO_DOWNLOADER

void MainWindow::CreateSearchBarAndPanel()
{
  CreatePanelImpl(0, Qt::RightDockWidgetArea, tr("Search"), QKeySequence(), 0);

  SearchPanel * panel = new SearchPanel(m_pDrawWidget, m_Docks[0]);
  m_Docks[0]->setWidget(panel);
}

void MainWindow::CreatePanelImpl(size_t i, Qt::DockWidgetArea area, QString const & name,
                                 QKeySequence const & hotkey, char const * slot)
{
  ASSERT_LESS(i, m_Docks.size(), ());
  m_Docks[i] = new QDockWidget(name, this);

  addDockWidget(area, m_Docks[i]);

  // hide by default
  m_Docks[i]->hide();

  // register a hotkey to show panel
  if (slot && !hotkey.isEmpty())
  {
    QAction * pAct = new QAction(this);
    pAct->setShortcut(hotkey);
    connect(pAct, SIGNAL(triggered()), this, slot);
    addAction(pAct);
  }
}

void MainWindow::CreateTrafficPanel(string const & dataFilePath, string const & sampleFilePath)
{
  CreatePanelImpl(1, Qt::RightDockWidgetArea, tr("Traffic"), QKeySequence(), nullptr);

  m_trafficMode = new TrafficMode(dataFilePath, sampleFilePath,
                                  m_pDrawWidget->GetFramework().GetIndex(),
                                  make_unique<TrafficDrawerDelegate>(*m_pDrawWidget));
  m_Docks[1]->setWidget(new TrafficPanel(m_trafficMode, m_Docks[1]));
  m_Docks[1]->adjustSize();
}

void MainWindow::DestroyTrafficPanel()
{
  removeDockWidget(m_Docks[1]);
  delete m_Docks[1];
  m_Docks[1] = nullptr;
}

void MainWindow::closeEvent(QCloseEvent * e)
{
  m_pDrawWidget->PrepareShutdown();
  e->accept();
}

void MainWindow::OnDownloadClicked()
{
  m_pDrawWidget->DownloadCountry(m_lastCountry);
}

void MainWindow::OnRetryDownloadClicked()
{
  m_pDrawWidget->RetryToDownloadCountry(m_lastCountry);
}

void MainWindow::OnTrafficEnabled()
{
  bool const enabled = m_trafficEnableAction->isChecked();
  m_pDrawWidget->GetFramework().GetTrafficManager().SetEnabled(enabled);
  m_pDrawWidget->GetFramework().SaveTrafficEnabled(enabled);
}

void MainWindow::OnOpenTrafficSample()
{
  TrafficModeInitDlg dlg;
  dlg.exec();
  if (dlg.result() != QDialog::DialogCode::Accepted)
    return;

  LOG(LDEBUG, ("Traffic mode enabled"));
  CreateTrafficPanel(dlg.GetDataFilePath(), dlg.GetSampleFilePath());
  m_quitTrafficModeAction->setEnabled(true);
  m_saveTrafficSampleAction->setEnabled(true);
  m_Docks[1]->show();
}

void MainWindow::OnSaveTrafficSample()
{
  auto const & fileName = QFileDialog::getSaveFileName(this, tr("Save sample"));
  if (fileName.isEmpty())
    return;

  if (!m_trafficMode->SaveSampleAs(fileName.toStdString()))
    ;// TODO(mgsergio): Show error dlg;
}

void MainWindow::OnQuitTrafficMode()
{
  // If not saved, ask a user if he/she wants to save.
  // OnSaveTrafficSample()
  m_quitTrafficModeAction->setEnabled(false);
  m_saveTrafficSampleAction->setEnabled(false);
  DestroyTrafficPanel();
  m_trafficMode = nullptr;
}
}  // namespace qt

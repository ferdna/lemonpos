/**************************************************************************
 *   Copyright © 2007-2010 by Miguel Chavez Gamboa                         *
 *   miguel@lemonpos.org                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/
#include "lemonview.h"
#include "settings.h"
#include "inputdialog.h"
#include "productdelegate.h"
#include "pricechecker.h"
#include "../dataAccess/azahar.h"
#include "../printing/print-dev.h"
#include "../printing/print-cups.h"
#include "ticketpopup.h"
#include "misc.h"
#include "hash.h"
#include "specialordereditor.h"
#include "soselector.h"
#include "sostatus.h"
#include "resume.h"
#include "reservations.h"
#include "../../mibitWidgets/mibittip.h"
#include "../../mibitWidgets/mibitpassworddlg.h"
#include "../../mibitWidgets/mibitfloatpanel.h"
#include "../../mibitWidgets/mibitnotifier.h"


//StarMicronics printers
// #include "printers/sp500.h"

#include <QtGui/QPrinter>
#include <QtGui/QPrintDialog>

#include <QWidget>
#include <QStringList>
#include <QTimer>
#include <QColor>
#include <QPixmap>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCodec>
#include <QRegExp>
#include <QRegExpValidator>
#include <QValidator>
#include <QGridLayout>
#include <QDesktopWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QTextDocument>
#include <QTextEdit>
#include <QPushButton>
#include <QDir>

#include <klocale.h>
#include <kiconloader.h>
#include <kstandarddirs.h>
#include <kmessagebox.h>
#include <kpassivepopup.h>
#include <KNotification>


/* Widgets zone                                                                                                         */
/*======================================================================================================================*/

///NOTE: Testing with KDE 4.2, this TicketPopup dialog was not shown. So it was changed.

class BalanceDialog : public QDialog
{
  private:
    QGridLayout *gridLayout;
    QTextEdit *editText;
    QPushButton *buttonClose;

  public:
    BalanceDialog(QWidget *parent=0, QString str="")
    {
      setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
      setWindowModality(Qt::ApplicationModal);

      gridLayout = new QGridLayout(this);
      editText = new QTextEdit(str);
      editText->setReadOnly(true);
      editText->setMinimumSize(QSize(320,450));
      gridLayout->addWidget(editText, 0, 0);
      buttonClose = new QPushButton(this);
      buttonClose->setText(i18n("Continue"));
      buttonClose->setDefault(true);
      buttonClose->setShortcut(Qt::Key_Enter);
      gridLayout->addWidget(buttonClose, 1,0);

      connect(buttonClose, SIGNAL(clicked()), this, SLOT(close()));
      //connect(buttonClose, SIGNAL(clicked()), parent, SLOT(slotDoStartOperation()));
    }
    virtual void paint(QPainter *) {}
  protected:
    void paintEvent(QPaintEvent *e)
    {
      QPainter painter;
      painter.begin(this);
      painter.setClipRect(e->rect());
      painter.setRenderHint(QPainter::Antialiasing);

      paint(&painter);
      painter.restore();
      painter.save();
      int level = 180;
      painter.setPen(QPen(QColor(level, level, level), 6));
      painter.setBrush(Qt::NoBrush);
      painter.drawRect(rect());
    }

};


void lemonView::cancelByExit()
{
 preCancelCurrentTransaction();
 Azahar * myDb = new Azahar;
 myDb->setDatabase(db);
 myDb->deleteEmptyTransactions();
  if (db.isOpen()) {
    qDebug()<<"Sending close connection to database...";
    db.close();
  }
}

lemonView::lemonView(QWidget *parent) //: QWidget(parent)
{
  qDebug()<<"===STARTING LEMON AT "<<QDateTime::currentDateTime().toString()<<" ===";
  drawerCreated=false;
  modelsCreated=false;
  currentBalanceId = 0;
  availabilityDoesNotMatters = false;
  doNotAddMoreItems = false;
  finishingReservation = false;
  startingReservation = false;
  reservationPayment = 0;
  reservationId = 0;
  QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
  db = QSqlDatabase::addDatabase("QMYSQL"); //moved here because calling multiple times cause a crash on certain installations (Not kubuntu 8.10).
  ui_mainview.setupUi(this);
  dlgLogin = new LoginWindow(this,
                             i18n("Welcome to Lemon"),
                             i18n("Enter username and password to start using the system."),
                             LoginWindow::FullScreen);
  dlgPassword = new LoginWindow(this,
                             i18n("Authorisation Required"),
                             i18n("Enter administrator password please."),
                             LoginWindow::PasswordOnly);

  //MibitTips
  QString path = KStandardDirs::locate("appdata", "styles/");
  path = path+"tip.svg";
  tipCode   = new MibitTip(this, ui_mainview.editItemCode, path, DesktopIcon("dialog-warning",32) );
  path = KStandardDirs::locate("appdata", "styles/")+"rotated_tip.svg";
  tipAmount = new MibitTip(this, ui_mainview.groupPayment, path, DesktopIcon("dialog-warning",32), tpAbove );

  QTimer::singleShot(1000, this, SLOT(setupGridView()));

  //MibitPasswordDialog
  path = KStandardDirs::locate("appdata", "styles/") + "dialog.svg";
  lockDialog = new MibitPasswordDialog(this, "text", path, DesktopIcon("object-locked",64));
  lockDialog->setSize(300,150);
  lockDialog->setTextColor("Yellow");//Ensure to pass a valid Qt-CSS color name.
  lockDialog->setShakeTTL(3000);
  connect(lockDialog, SIGNAL(returnPressed()), this, SLOT(unlockScreen()));

  //MibitFloatPanel
  path = KStandardDirs::locate("appdata", "styles/");
  path = path+ "tip.svg"; //"panel_top.svg"; //or use the floating_top?
  currencyPanel = new MibitFloatPanel(ui_mainview.stackedWidget, path, Top);
  currencyPanel->setSize(200,211);
  currencyPanel->addWidget(ui_mainview.frameCurrency);
  currencyPanel->setMode(pmManual);
  currencyPanel->setHiddenTotally(true);
  currencyPanel->hide();

  //float panel for new discounts.
  discountPanel = new MibitFloatPanel(ui_mainview.frame, path, Top);
  discountPanel->setSize(550,250);
  discountPanel->addWidget(ui_mainview.discountWidget);
  ui_mainview.rbCoupon->setVisible(false);
  discountPanel->setMode(pmManual);
  discountPanel->setHiddenTotally(true);
  discountPanel->hide();
  connect(ui_mainview.rbPercentage, SIGNAL(toggled()), SLOT(changeDiscValidator()) );
  connect(ui_mainview.rbMoney, SIGNAL(toggled()), SLOT(changeDiscValidator()) );
  connect(ui_mainview.rbCoupon, SIGNAL(toggled()), SLOT(changeDiscValidator()) );
  oDiscountMoney = 0;

  path = KStandardDirs::locate("appdata", "styles/");
  path = path+"tip.svg";
  notifierPanel = new MibitNotifier(this,path, DesktopIcon("dialog-warning", 32));

  refreshTotalLabel();
  QTimer::singleShot(1000, this, SLOT(setupDB()));
  setAutoFillBackground(true);
  QTimer::singleShot(1500, this, SLOT(login()));
  QTimer *timerClock = new QTimer(this);

  loggedUserRole = roleBasic;
  
  //Signals
  connect(timerClock, SIGNAL(timeout()), SLOT(timerTimeout()) );
  //connect(ui_mainview.editItemDescSearch, SIGNAL(returnPressed()), this, SLOT(doSearchItemDesc()));
  connect(ui_mainview.editItemDescSearch, SIGNAL(textEdited(const QString&)), this, SLOT(doSearchItemDesc()));
  connect(ui_mainview.editItemCode, SIGNAL(returnPressed()), this, SLOT(doEmitSignalQueryDb()));
  connect(this, SIGNAL(signalQueryDb(QString)), this, SLOT(insertItem(QString)) );
  connect(ui_mainview.tableWidget, SIGNAL(itemDoubleClicked(QTableWidgetItem*)), SLOT(itemDoubleClicked(QTableWidgetItem*)) );
  connect(ui_mainview.tableSearch, SIGNAL(itemDoubleClicked(QTableWidgetItem*)), SLOT(itemSearchDoubleClicked(QTableWidgetItem*)) );
  connect(ui_mainview.tableWidget, SIGNAL(itemClicked(QTableWidgetItem*)), SLOT(displayItemInfo(QTableWidgetItem*)));
  //connect(ui_mainview.listView, SIGNAL(activated(const QModelIndex &)), SLOT(listViewOnClick(const QModelIndex &)));
  connect(ui_mainview.listView, SIGNAL(clicked(const QModelIndex &)), SLOT(listViewOnClick(const QModelIndex &)));
  connect(ui_mainview.listView, SIGNAL(entered(const QModelIndex &)), SLOT(listViewOnMouseMove(const QModelIndex &)));
  connect(ui_mainview.buttonSearchDone, SIGNAL(clicked()), SLOT(buttonDone()) );
  connect(ui_mainview.checkCard, SIGNAL(toggled(bool)), SLOT(checksChanged())  );
  connect(ui_mainview.checkCash, SIGNAL(toggled(bool)), SLOT(checksChanged())  );
  connect(ui_mainview.editAmount,SIGNAL(returnPressed()), SLOT(finishCurrentTransaction()) );
  connect(ui_mainview.editAmount, SIGNAL(textChanged(const QString &)), SLOT(refreshTotalLabel()));
  connect(ui_mainview.editCardNumber, SIGNAL(returnPressed()), SLOT(goSelectCardAuthNumber()) );
  connect(ui_mainview.editCardAuthNumber, SIGNAL(returnPressed()), SLOT(finishCurrentTransaction()) );
  connect(ui_mainview.splitter, SIGNAL(splitterMoved(int, int)), SLOT(setUpTable()));
  connect(ui_mainview.splitterGrid, SIGNAL(splitterMoved(int, int)), SLOT(setUpTable()));
  connect(ui_mainview.comboClients, SIGNAL(currentIndexChanged(int)), SLOT(comboClientsOnChange()));
  connect(ui_mainview.btnChangeSaleDate, SIGNAL(clicked()), SLOT(showChangeDate()));

  ui_mainview.editTicketDatePicker->setDate(QDate::currentDate());
  connect(ui_mainview.editTicketDatePicker, SIGNAL(dateChanged(const QDate &)), SLOT(setHistoryFilter()) );
  connect(ui_mainview.btnTicketDone, SIGNAL(clicked()), SLOT(btnTicketsDone()) );
  connect(ui_mainview.btnTicketPrint, SIGNAL(clicked()), SLOT(printSelTicket()) );
  connect(ui_mainview.ticketView, SIGNAL(doubleClicked(const QModelIndex &)), SLOT(itemHIDoubleClicked(const QModelIndex &)) );

  connect(ui_mainview.editItemCode, SIGNAL(plusKeyPressed()), this, SLOT(plusPressed()));

  connect(ui_mainview.btnCurrency, SIGNAL( clicked() ), currencyPanel, SLOT( showPanel() ) );
  connect(ui_mainview.btnCurrency, SIGNAL( clicked() ), this, SLOT( getCurrencies() ) );
  connect(ui_mainview.btnConvCancel, SIGNAL( clicked() ), currencyPanel, SLOT( hidePanel() ) );
  connect(ui_mainview.comboCurrency, SIGNAL(currentIndexChanged(int)), this, SLOT(comboCurrencyOnChange()) );
  connect(ui_mainview.editConvQty, SIGNAL(textEdited(const QString&)), SLOT( doCurrencyConversion() )  );
  connect(ui_mainview.btnConvOk, SIGNAL(clicked()), SLOT( acceptCurrencyConversion() )  );
  connect(ui_mainview.editConvQty, SIGNAL(returnPressed()), SLOT(acceptCurrencyConversion()) );

  connect(ui_mainview.btnApplyDiscount, SIGNAL(clicked()), SLOT(applyOccasionalDiscount() ) );
  connect(ui_mainview.editDiscount, SIGNAL(returnPressed()), SLOT(applyOccasionalDiscount() ) );
  connect(ui_mainview.btnCancelDiscount, SIGNAL(clicked()), discountPanel, SLOT(hidePanel() ) );

  timerClock->start(1000);

  drawer = new Gaveta();
  drawer->setPrinterDevice(Settings::printerDevice());
  //NOTE: setPrinterDevice: what about CUPS printers recently added support for?
  drawerCreated = true;
  
  operationStarted = false;
  productsHash.clear();
  specialOrders.clear();
  clientsHash.clear();
  //ui_mainview.lblClientPhoto->hide();
  //ui_mainview.labelInsertCodeMsg->hide();
  transDateTime = QDateTime::currentDateTime();
  ui_mainview.editTransactionDate->setDateTime(transDateTime);
  ui_mainview.groupSaleDate->hide();


  ui_mainview.editItemCode->setEmptyMessage(i18n("Enter code or qty*code. <Enter> or <+> Keys to go pay"));
  ui_mainview.editItemCode->setToolTip(i18n("Enter code or qty*code. <Enter> or <+> Keys to go pay"));

  clearUsedWidgets();
  loadIcons();
  setUpInputs();
  QTimer::singleShot(500, this, SLOT(setUpTable()));
  ui_mainview.groupWidgets->setCurrentIndex(pageMain);
  ui_mainview.mainPanel->setCurrentIndex(pageMain);

  // point the public ui pointers
  frameLeft = ui_mainview.frameLeft;
  frame     = ui_mainview.frame;


  //hide or show the subtotal labels
  if (!Settings::addTax()) {
    ui_mainview.lblSubtotalPre->hide();
    ui_mainview.lblSubtotal->hide();
    qDebug()<<"hiding subtotal label, not using addTax option.";
  }


  //excluded list for the random messages on tickets.
  rmExcluded.clear();
  //NOTE: this list is populated by the Azahar::getRandomMessage() method.
  //calculate the season... Here are different ways, by months -the easier- or by groups of months.
  QDate today = QDate::currentDate();
  rmSeason = today.month();

 //  switch (today.month()) {
 //    //-- Christmas time
 //    case 12:
 //    case  1:
 //    case  2:
 //    //--
 //    case 3:
 //    case 4:
 //    case 5:
 //    case 6:
 //    case 7:
 //    case 8:
 //    case 9:
 //    case 10:
 //    case 11:
 //    }

  ui_mainview.editItemCode->setFocus();
}

void lemonView::showChangeDate()
{
  ui_mainview.groupSaleDate->show();
}

void lemonView::setupGridView()
{
  if (Settings::showGrid()) emit signalShowProdGrid();
  else showProductsGrid(false);
  
}

void lemonView::loadIcons()
{
  ui_mainview.labelImageSearch->setPixmap(DesktopIcon("edit-find", 64));
  QString logoBottomFile = KStandardDirs::locate("appdata", "images/logo_bottom.png");
  ui_mainview.labelBanner->setPixmap(QPixmap(logoBottomFile));
  ui_mainview.labelBanner->setAlignment(Qt::AlignCenter);
}

void lemonView::setUpTable()
{
  QSize tableSize = ui_mainview.tableWidget->size();
  int portion = tableSize.width()/10;
  ui_mainview.tableWidget->horizontalHeader()->setResizeMode(QHeaderView::Interactive);
  ui_mainview.tableWidget->horizontalHeader()->resizeSection(colCode, portion); //BAR CODE
  ui_mainview.tableWidget->horizontalHeader()->resizeSection(colDesc, (portion*4)+9); //DESCRIPTION
  ui_mainview.tableWidget->horizontalHeader()->resizeSection(colPrice, portion); //PRICE
  ui_mainview.tableWidget->horizontalHeader()->resizeSection(colQty, portion-20);  //QTY
  ui_mainview.tableWidget->horizontalHeader()->resizeSection(colUnits, portion-15);//UNITS
  ui_mainview.tableWidget->horizontalHeader()->resizeSection(colDisc, portion); //Discount
  ui_mainview.tableWidget->horizontalHeader()->resizeSection(colDue, portion+10); //DUE
  //search table
  tableSize = ui_mainview.tableSearch->size();
  ui_mainview.tableSearch->horizontalHeader()->setResizeMode(QHeaderView::Interactive);
  ui_mainview.tableSearch->horizontalHeader()->resizeSection(0, 2*(tableSize.width()/4));
  ui_mainview.tableSearch->horizontalHeader()->resizeSection(1, tableSize.width()/4);
  ui_mainview.tableSearch->horizontalHeader()->resizeSection(2, tableSize.width()/4);
}

void lemonView::setUpInputs()
{
  //TODO: Tratar de poner un filtro con lugares llenos de ceros, e ir insertando los numeros.
  //For amount received.
  QRegExp regexpA("[0-9]*[//.]{0,1}[0-9]{0,5}"); //QRegExp regexpA("[0-9]*[//.]{0,1}[0-9][0-9]*"); //Cualquier numero flotante (0.1, 100, 0, .10, 100.0, 12.23)
  QRegExpValidator * validatorFloat = new QRegExpValidator(regexpA,this);
  ui_mainview.editAmount->setValidator(validatorFloat);
  //Item code (to insert) //
  //QRegExp regexpC("[0-9]+[0-9]*[//.]{0,1}[0-9]{0,2}[//*]{0,1}[0-9]*[A-Za-z_0-9\\\\/\\-]{0,30}"); // Instead of {0,13} fro EAN13, open for up to 30 chars.
  QRegExp regexpC("[0-9]*[//.]{0,1}[0-9]{0,5}[//*]{0,1}[0-9]*[A-Za-z_0-9\\\\/\\-]{0,30}"); // Instead of {0,13} fro EAN13, open for up to 30 chars.
  //NOTE: We remove the xX from the regexp for use as the separator between qtys and code. Only * can be used now, for Alphacode support
  QRegExpValidator * validatorEAN13 = new QRegExpValidator(regexpC, this);
  ui_mainview.editItemCode->setValidator(validatorEAN13);
  QRegExp regexpAN("[A-Za-z_0-9\\\\/\\-]+");//any letter, number, both slashes, dash and lower dash.
  QRegExpValidator *regexpAlpha = new QRegExpValidator(regexpAN, this);
  ui_mainview.editCardAuthNumber->setValidator(regexpAlpha);

  //ui_mainview.editAmount->setInputMask("000,000.00");

 //filter for new discount edit line. at the begining, it is by percentage discount the default.
  QDoubleValidator *doubleVal = new QDoubleValidator(0.001, 99.00, 4, this);
  ui_mainview.editDiscount->setValidator(doubleVal);
}

void lemonView::changeDiscValidator()
{
    if (ui_mainview.rbPercentage->isChecked()) {
        QDoubleValidator *doubleVal = new QDoubleValidator(0.00001, 99.00000, 5, this);
        ui_mainview.editDiscount->setValidator(doubleVal);
    }
    else if (ui_mainview.rbMoney->isChecked()) {
        QDoubleValidator *doubleVal = new QDoubleValidator(0.001, 9999999.0, 5, this);
        ui_mainview.editDiscount->setValidator(doubleVal);
    }
    else {
        //a string...
        ui_mainview.editDiscount->setValidator(0); //remove any validator.
    }
}

void lemonView::timerTimeout()
{
  emit signalUpdateClock();
}

void::lemonView::clearLabelSearchMsg()
{
  ui_mainview.labelSearchMsg->clear();
}

void lemonView::setTheSplitterSizes(QList<int> s)
{
  ui_mainview.splitter->setSizes(s);
}

QList<int> lemonView::getTheSplitterSizes()
{
  return ui_mainview.splitter->sizes();
}

void lemonView::setTheGridSplitterSizes(QList<int> s)
{
  ui_mainview.splitterGrid->setSizes(s);
}

QList<int> lemonView::getTheGridSplitterSizes()
{
  return ui_mainview.splitterGrid->sizes();
}

//This ensures that when not connected to mysql, the user can configure the db settings and then trying to connect
//with the new settings...
void lemonView::settingsChanged()
{
  //Total label (and currency label)
  refreshTotalLabel();

  ///This is a temporal workaround for the crash. I think is due to a kde bug. It does not crashes with kde 4.1.4 on kubuntu
  //Reconnect to db..
  if (db.isOpen()) db.close();
  qDebug()<<"-Config Changed- reconnecting to database..";

  db.setHostName(Settings::editDBServer());
  db.setDatabaseName(Settings::editDBName());
  db.setUserName(Settings::editDBUsername());
  db.setPassword(Settings::editDBPassword());
  connectToDb();
  setupModel();
  setupHistoryTicketsModel();
  
  currentBalanceId = 0;
  insertBalance(); //this updates the currentBalanceId
  startAgain();

  syncSettingsOnDb();

}

void lemonView::syncSettingsOnDb()
{
  //save new settings on db -to avoid double settings on lemon and squeeze-
  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  if (!Settings::storeLogo().isEmpty()) {
    QPixmap p = QPixmap( Settings::storeLogo() );
    myDb->setConfigStoreLogo(Misc::pixmap2ByteArray(new QPixmap(p), p.size().width(),p.size().height()));
  }
  myDb->setConfigStoreName(Settings::editStoreName());
  myDb->setConfigStoreAddress(Settings::storeAddress());
  myDb->setConfigStorePhone(Settings::storePhone());
  myDb->setConfigSmallPrint(!Settings::bigReceipt());
  myDb->setConfigUseCUPS(!Settings::smallTicketDotMatrix());
  myDb->setConfigLogoOnTop(Settings::chLogoOnTop());
  myDb->setConfigTaxIsIncludedInPrice(!Settings::addTax());//NOTE: the AddTax means the tax is NOT included in price, thats why this is negated.
  
  delete myDb;
}

void lemonView::settingsChangedOnInitConfig()
{
  qDebug()<<"==> Initial Config Changed- connecting to database and calling login...";
  //the db = QSqlDatabase... thing is causing a crash.
  //QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
  //db = QSqlDatabase::addDatabase("QMYSQL");
  db.setHostName(Settings::editDBServer());
  db.setDatabaseName(Settings::editDBName());
  db.setUserName(Settings::editDBUsername());
  db.setPassword(Settings::editDBPassword());

  ///This is also affected by the weird crash.
  connectToDb();
  setupModel();
  setupHistoryTicketsModel();

  currentBalanceId = 0;
  insertBalance(); //this updates the currentBalanceId
  startAgain();

  emit signalDisableStartOperationAction();
  login();
}

void lemonView::showEnterCodeWidget()
{
  ui_mainview.groupWidgets->setCurrentIndex(pageMain);
  // BFB. Toggle editItemCode and editFilterByDesc.
  if (ui_mainview.editItemCode->hasFocus()){
    ui_mainview.rbFilterByDesc->setChecked(true);
    ui_mainview.editFilterByDesc->setFocus();
  }else
    ui_mainview.editItemCode->setFocus();
  setUpTable();
}

void lemonView::showSearchItemWidget()
{
  ui_mainview.mainPanel->setCurrentIndex(pageSearch); // searchItem
  ui_mainview.editItemDescSearch->setFocus();
  setUpTable();
}

void lemonView::buttonDone()
{
  ui_mainview.tableSearch->setRowCount(0);
  ui_mainview.labelSearchMsg->setText("");
  ui_mainview.editItemDescSearch->setText("");
  ui_mainview.editItemCode->setCursorPosition(0);
  ui_mainview.mainPanel->setCurrentIndex(0); // back to welcome widget
}

void lemonView::checksChanged()
{
  if (ui_mainview.checkCash->isChecked())
  {
    ui_mainview.stackedWidget->setCurrentIndex(0);
    ui_mainview.editAmount->setFocus();
    ui_mainview.editAmount->setSelection(0,ui_mainview.editAmount->text().length());
  }//cash
  else  //Card, need editCardkNumber...
  {
    ui_mainview.stackedWidget->setCurrentIndex(1);
    ui_mainview.editAmount->setText(QString::number(totalSum));
    ui_mainview.editCardNumber->setFocus();
    ui_mainview.editCardNumber->setSelection(0,ui_mainview.editCardNumber->text().length());
  }
  refreshTotalLabel();
}

void lemonView::clearUsedWidgets()
{
  ui_mainview.editAmount->setText("");
  ui_mainview.editCardNumber->setText("");
  ui_mainview.editCardAuthNumber->setText("");
  ui_mainview.tableWidget->clearContents();
  ui_mainview.tableWidget->setRowCount(0);
  totalSum = 0.0;
  buyPoints = 0;
  ui_mainview.labelDetailTax1->setText("");
  ui_mainview.labelDetailTax2->setText("");
  ui_mainview.labelDetailUnits->setText("");
  ui_mainview.labelDetailDesc->setText(i18n("No product selected"));
  ui_mainview.labelDetailPrice->setText("");
  ui_mainview.labelDetailDiscount->setText("");
  ui_mainview.labelDetailTotalTaxes->setText("");
  ui_mainview.labelDetailPhoto->clear();
  ui_mainview.labelDetailPoints->clear();

  //enable clients combo box...
  ui_mainview.comboClients->setEnabled(true);
}

void lemonView::askForIdToCancel()
{
  bool continuar=false;
  if (Settings::lowSecurityMode()) {//qDebug()<<"LOW security mode";
    continuar=true;
  } else if (Settings::requiereDelAuth()) {// qDebug()<<"NO LOW security mode, but AUTH REQUIRED!";
    dlgPassword->show();
    dlgPassword->hide();
    dlgPassword->clearLines();
    continuar = dlgPassword->exec();
  } else {//     qDebug()<<"NO LOW security mode, NO AUTH REQUIRED...";
    continuar=true;
  }

  if (continuar) { //show input dialog to get ticket number
    bool ok=false;
    qulonglong id = 0;
    InputDialog *dlg = new InputDialog(this, true, dialogTicket, i18n("Enter the ticket number to cancel"));
    if (dlg->exec())
    {
      id = dlg->iValue;
      ok = true;
    }
    if (ok) {                 // NOTE :
      cancelTransaction(id); //Mark as cancelled in database..  is this transaction
                            //done in the current operation, or a day ago, a month ago, 10 hours ago?
                           //Allow cancelation of same day of sell, or older ones too?
    }//ok=true
  } //continuar
}

///NOTE: Not implemented yet
void lemonView::askForTicketToReturnProduct()
{
  bool continuar=false;
  if (Settings::lowSecurityMode()) {//qDebug()<<"LOW security mode";
    continuar=true;
  } else if (Settings::requiereDelAuth()) {// qDebug()<<"NO LOW security mode, but AUTH REQUIRED!";
    dlgPassword->show();
    dlgPassword->hide();
    dlgPassword->clearLines();
    continuar = dlgPassword->exec();
  } else {//     qDebug()<<"NO LOW security mode, NO AUTH REQUIRED...";
    continuar=true;
  }
  
  if (continuar) { //show input dialog to get ticket number
    bool ok=false;
    qulonglong id = 0;
    InputDialog *dlg = new InputDialog(this, true, dialogTicket, i18n("Enter the ticket number"));
    if (dlg->exec())
    {
      id = dlg->iValue;
      ok = true;
    }
    if (ok) {
      // show dialog to select which items to return.
      
    }//ok=true
  } //continuar
}

void lemonView::focusPayInput()
{
  ui_mainview.groupWidgets->setCurrentIndex(pageMain);
  ui_mainview.editAmount->setFocus();
  ui_mainview.editAmount->setSelection(0, ui_mainview.editAmount->text().length());
}

//This method sends the focus to the amount to be paid only when the code input is empty.
void lemonView::plusPressed()
{
  if ( !ui_mainview.editItemCode->text().isEmpty() )
    doEmitSignalQueryDb();
  else 
    focusPayInput();
}

void lemonView::goSelectCardAuthNumber()
{
  ui_mainview.editCardAuthNumber->setFocus();
}


void lemonView::getCurrencies()
{
    Azahar *myDb = new Azahar();
    myDb->setDatabase(db);
    
    //get currencies from database
    QList<CurrencyInfo> currencyList = myDb->getCurrencyList();
    //load currencies to combobox
    if ( ui_mainview.comboCurrency->count() > 0 ) ui_mainview.comboCurrency->clear();
    foreach( CurrencyInfo info, currencyList ) {
        ui_mainview.comboCurrency->addItem( info.name );
    }
    
    //select first one and set the factor to the edit.
    if ( !currencyList.isEmpty() ) {
        CurrencyInfo info = currencyList.first();
        ui_mainview.editConvFactor->setText( QString::number(info.factor) );
    }

    ui_mainview.editConvQty->setFocus();

    delete myDb;
}

void lemonView::comboCurrencyOnChange()
{
    Azahar *myDb = new Azahar();
    myDb->setDatabase(db);

    CurrencyInfo info = myDb->getCurrency( ui_mainview.comboCurrency->currentText() );
    ui_mainview.editConvFactor->setText( QString::number( info.factor ) );
    
    doCurrencyConversion();

    ui_mainview.editConvQty->setFocus();

    delete myDb;
}

void lemonView::doCurrencyConversion()
{
    if ( !ui_mainview.editConvQty->text().isEmpty() && !ui_mainview.editConvFactor->text().isEmpty() ) {
        double qty    = 0;
        double factor = 0;
        double result = 0;
        qty    = ui_mainview.editConvQty->text().toDouble();
        factor = ui_mainview.editConvFactor->text().toDouble();
        result = qty * factor;
        ui_mainview.editConvResult->setText( QString::number( result ) );
        qDebug()<<"CURRENCY CONVERSION -- qty:"<<qty<<" factor:"<<factor<<" RESULT:"<<result<<" Result converted to string:"<<QString::number(result);
    }
}

void lemonView::acceptCurrencyConversion()
{
    ui_mainview.editAmount->setText( ui_mainview.editConvResult->text() );
    currencyPanel->hidePanel();
    ui_mainview.editAmount->setFocus();
}


lemonView::~lemonView()
{
  drawerCreated=false;
  delete drawer;
}


/* Users zone                                                                                                          */
/*=====================================================================================================================*/

QString lemonView::getLoggedUser()
{
  return loggedUser;
}

QString lemonView::getLoggedUserName(QString id)
{
  QString uname = "";
  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  uname = myDb->getUserName(id);
  delete myDb;
  return uname;
}

int lemonView::getUserRole(qulonglong id)
{
  int role = 0;
  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  role = myDb->getUserRole(id);
  delete myDb;
  return role;
}

qulonglong lemonView::getLoggedUserId(QString uname)
{
  unsigned int iD=0;
  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  iD = myDb->getUserId(uname);
  delete myDb;
  return iD;
}

void lemonView::login()
{
  qDebug()<<"Login.. emiting disable startop action";
  emit signalDisableStartOperationAction();

  corteDeCaja(); //Make a corteDeCaja "BALANCE"
  loggedUser = "";
  loggedUserName ="";
  loggedUserRole = roleBasic;

  //qDebug()<<"In Cash:"<<drawer->getAvailableInCash()<<"Transactions:"<<drawer->getTransactionsCount();
  drawer->reset();
  emit signalNoLoggedUser();
  
  dlgLogin->clearLines();
  if (!db.isOpen()) {
      qDebug()<<"(login): Calling connectToDb()...";
      connectToDb();
  }

  if (!db.isOpen()) {
    qDebug()<<"(login): Still unable to open connection to database....";
    QString msg = i18n("Could not connect to database, please press 'login' button again to raise a database configuration.");
    KPassivePopup::message( i18n("Error:"),msg, DesktopIcon("dialog-error", 48), this );
  } else {
    if ( dlgLogin->exec() ) {
      loggedUser     = dlgLogin->username();
      loggedUserName = getLoggedUserName(loggedUser);
      loggedUserId   = getLoggedUserId(loggedUser);
      loggedUserRole = getUserRole(loggedUserId);
      emit signalLoggedUser();
      //Now check roles instead of names
      if (loggedUserRole == roleAdmin) {
	emit signalAdminLoggedOn();
	//if (!canStartSelling()) startOperation();
      } else {
	emit signalAdminLoggedOff();
        if (loggedUserRole == roleSupervisor)
          emit signalSupervisorLoggedOn();
	else {
          emit signalEnableStartOperationAction();
          //slotDoStartOperation();
        }
      }
    } else {
      loggedUser ="";
      loggedUserName = "";
      loggedUserId = 0;
      loggedUserRole = roleBasic;
      emit signalNoLoggedUser();
      if (dlgLogin->wantToQuit()) qApp->quit();
    }
  }
}

bool lemonView::validAdminUser()
{
  bool result = false;
  if (Settings::lowSecurityMode()) result = true;
  else {
      dlgPassword->show();
      dlgPassword->hide();
      dlgPassword->clearLines();
      if (dlgPassword->exec())  result = true;
  }
  return result;
}

/* Item things: shopping list, search, insert, delete, calculate total */
/*--------------------------------------------------------------------*/

void lemonView::doSearchItemDesc()
{
  //clear last search
  ui_mainview.tableSearch->clearContents();
  ui_mainview.tableSearch->setRowCount(0);
  //Search
  QString desc = ui_mainview.editItemDescSearch->text();
  QRegExp regexp = QRegExp(desc);
    if (!regexp.isValid() || desc.isEmpty())  desc = "*";
  if (!db.isOpen()) db.open();

  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  QList<qulonglong> pList = myDb->getProductsCode(desc); //busca con regexp...
  int numRaw = 0;
  //iteramos la lista
  for (int i = 0; i < pList.size(); ++i) {
     qulonglong c = pList.at(i);
     ProductInfo pInfo = myDb->getProductInfo(QString::number(c));
     if (pInfo.isARawProduct) numRaw++;
     if (pInfo.code==0 || pInfo.isARawProduct) continue; //discard this item, continue loop.
     //insert each product to the search table...
     int rowCount = ui_mainview.tableSearch->rowCount();
      ui_mainview.tableSearch->insertRow(rowCount);
      QTableWidgetItem *tid = new QTableWidgetItem(pInfo.desc);
      if (pInfo.stockqty == 0) {
        QBrush b = QBrush(QColor::fromRgb(255,100,0), Qt::SolidPattern);
        tid->setBackground(b);
      }
      else if (pInfo.stockqty > 0 && pInfo.stockqty < Settings::stockAlertValue() ) {//NOTE:This must be shared between lemon and squeeze
        QBrush b = QBrush(QColor::fromRgb(255,176,73), Qt::SolidPattern);
        tid->setBackground(b);
      }
      ui_mainview.tableSearch->setItem(rowCount, 0, tid);
      //NOTE:bug fixed Sept 26 2008: Without QString::number, no data was inserted.
      // if it is passed a numer as the only parameter to QTableWidgetItem, it is taken as a type
      // and not as a data to display.
      ui_mainview.tableSearch->setItem(rowCount, 1, new QTableWidgetItem(QString::number(pInfo.price)));
      ui_mainview.tableSearch->setItem(rowCount, 2, new QTableWidgetItem(QString::number(pInfo.code)));
      ui_mainview.tableSearch->resizeRowsToContents();
    }
    if (pList.count()>0) ui_mainview.labelSearchMsg->setText(i18np("%1 item found","%1 items found.", pList.count()-numRaw));
    else ui_mainview.labelSearchMsg->setText(i18n("No items found."));

    delete myDb;
  }

int lemonView::getItemRow(QString c)
{
  int result = 0;
  for (int row=0; row<ui_mainview.tableWidget->rowCount(); ++row)
  {
    QTableWidgetItem * item = ui_mainview.tableWidget->item(row, colCode);
    QString icode = item->data(Qt::DisplayRole).toString();
    if (icode == c) {
      result = row;
      break;
    }
  }
  return result; //0 if not found
}

void lemonView::refreshTotalLabel()
{
    //BEGIN of REWRITE: This code has been rewritten from scratch. Aug 15 2011.

    ///NOTE: FIXME! The nonDiscountable items should not be discounted with dollar discount or Percentage discount.
    ///             Now the problem is with special Orders. How to allow/disallow discounts for them?
    totalSum = 0;
    totalTax = 0;
    totalSumWODisc = 0;
    discMoney = 0;
    double sum = 0;
    double sum_pre=0;
    double taxes = 0;
    int nonDiscountables = 0;
    double gDiscountPercentage = 0;

    ///We need to iterate the products to get the nonDiscountable items, and the totals.
    //FIXME: Still missing the Special Orders iteration likke this one.
    foreach(ProductInfo prod, productsHash) {
        if (prod.isNotDiscountable)
            nonDiscountables++;

        double iPrice   = prod.price;
        //Get out tax if is included in price.
        if (!Settings::addTax())
            iPrice = prod.price/(1+((prod.tax+prod.extratax)/100));
        //if item has discount, apply it.
        if (prod.validDiscount && !prod.isNotDiscountable)
            iPrice -= (prod.discpercentage/100)*prod.price;
        if (!prod.validDiscount && prod.disc > 0 && !prod.isNotDiscountable) {///if this is true, then there is a product price change.
            iPrice -= prod.disc; //product price changed.
            discMoney += prod.disc;
        }

        sum_pre += iPrice * prod.qtyOnList;
    }

    bool notApply = false;
    if ((nonDiscountables == productsHash.count() && !productsHash.isEmpty())  || (nonDiscountables == specialOrders.count() && !specialOrders.isEmpty() ))
        notApply = true;
    
    ///Now we need to get and apply GENERAL DISCOUNTS (applied to the whole sale) like client discount or ocassional discounts.
    double gDiscount = 0; //in money.
    if (clientInfo.discount >0 && !notApply)
        gDiscountPercentage = clientInfo.discount/100;
    if (oDiscountMoney > 0 && !notApply)
        gDiscount += oDiscountMoney;
    
    //get the DOLLAR discount in percentage.
    if (gDiscount > 0)  //if 0, it means the gDiscountPercentage is already calculated in previous steps... or is really 0.
        gDiscountPercentage = gDiscount/sum_pre;
    
    qDebug()<<"SUM_PRE:"<<sum_pre<<" DOLLAR DISCOUNT TO PERCENTAGE:"<<gDiscountPercentage;
    qDebug()<<"notApply:"<<notApply<<" nonDiscountables:"<<nonDiscountables<<" clientInfo.discount:"<<clientInfo.discount<<" oDiscountMoney:"<<oDiscountMoney<<" gDiscount:"<<gDiscount;
    
    ///iterate each product in the hash. to calculate the total sum and taxes.
    QHashIterator<qulonglong, ProductInfo> i(productsHash);
    while (i.hasNext()) {
        i.next();
        ProductInfo prod = i.value();
        double iPrice   = prod.price;
        //Get out tax if is included in price.
        if (!Settings::addTax())
            iPrice = prod.price/(1+((prod.tax+prod.extratax)/100));

        double iPriceWD = iPrice; //price without discounts and taxes (if addtax...).

        //if item has discount, apply it.
        if (prod.validDiscount && !prod.isNotDiscountable)
            iPrice -= (prod.discpercentage/100)*prod.price;
        if (!prod.validDiscount && prod.disc > 0 && !prod.isNotDiscountable) ///if this is true, then there is a product price change.
            iPrice -= prod.disc; //product price changed.
        if ( gDiscountPercentage > 0 ) //apply general discount. gDiscountPercentage is in PERCENTAGE Already... do not divide by 100. 
            iPrice -= (gDiscountPercentage)*prod.price; ///NOTE: if gDiscountPercentage is grater than 1 then it will produce negative price!

        //item taxes.  totalTax is the tax in money (discount applied if apply) for the qtyOnList items
        ///Now tax is calculated over the price with discounts including general discount (percentage or dollar and price changes). MCH Aug 30 2011.
        double iTax = ( ((prod.tax/100)*iPrice) + ((prod.extratax/100)*iPrice) ) * prod.qtyOnList;
        taxes += iTax; //to debug it.
        
        ///To calculate the sum we sum prices with included discounts per item. It does not includes taxes if Settings:addTax is false.
        sum += iPrice * prod.qtyOnList;
        totalSumWODisc += iPriceWD * prod.qtyOnList; //total sum without discounts and taxes if addtax...

        buyPoints += (prod.points*prod.qtyOnList);

        qDebug()<<prod.desc<<" - Price without Tax and discounts:"<<iPrice<<" item Tax $:"<<iTax<<"  Accumulated Tax in the purchase:"<<taxes<< " Purchase Accumulated:"<<sum;
    }
    qDebug()<<" SUM:"<<sum;
    //now iterate the SO. In case there are SO then no products are allowed, sum will be 0 at this point.
    foreach(SpecialOrderInfo soInfo, specialOrders) {
        double iPrice   = soInfo.payment;
        if (!Settings::addTax())
            iPrice = soInfo.payment/(1+((soInfo.averageTax)/100));
        double iPriceWD = iPrice; //price without discounts and taxes (if addtax...).
        //get discount
        Azahar *myDb = new Azahar;
        myDb->setDatabase(db);

        double soDiscount  = myDb->getSpecialOrderAverageDiscount(soInfo.orderid)/100;
        nonDiscountables += myDb->getSpecialOrderNonDiscountables(soInfo.orderid);
        
        if (soDiscount > 0)
            iPrice -= soDiscount*iPrice;
        //client and ocassional discounts.
        if (clientInfo.discount>0)
            iPrice -= (clientInfo.discount/100)*iPrice; //applied over the discounted price (if it has discount)
        ///NOTE: SO are not allowed (implemented) for price change.
        
        sum += iPrice * soInfo.qty;
        totalSumWODisc += iPriceWD * soInfo.qty; //total sum without discounts and taxes if addtax...
        taxes += myDb->getSpecialOrderAverageTax(soInfo.orderid, rtMoney);
        //buyPoints += (info.points*info.qtyOnList); NOTE & FIXME: SO does not get the POINTS. we would need to get frmo the elements (raw products).
        delete myDb;
        qDebug()<<"<SO> Price without tax and discounts: $"<<iPrice<<" Tax:$"<<myDb->getSpecialOrderAverageTax(soInfo.orderid, rtMoney)<<" Accumulated tax:$"<<taxes<<" Accumulated Purchase:"<<sum;
    }

    if (notApply && (gDiscount > 0 || gDiscountPercentage>0))
    {
        //Hey, only nonDiscountable items! do not apply discounts!
        ///WARNING: Problem when SO contains one non discountable item, and two or more SOs. REVIEW THIS CASE!
        notifierPanel->setSize(350,150);
        notifierPanel->setOnBottom(false);
        notifierPanel->showNotification("<b>Cannot apply discount</b> to a product marked as not discountable.",5000);
    }

    if (gDiscountPercentage > 0)
        discMoney += gDiscountPercentage * sum; //until now, discMoney has price change discount OR Global Dollar discount OR Global Percentage discount
    ///we get subtotal.
    totalTax       = taxes;
    subTotalSum    = sum - reservationPayment; //- gDiscount: ya se incluyo en la iteracion...
    if (Settings::addTax())
        totalSum   = subTotalSum + totalTax;
    else totalSum = subTotalSum;
    
    qDebug()<<" RESERVATION PAYMENT:"<<reservationPayment;

    ///refresh labels.
    ui_mainview.labelTotal->setText(QString("%1").arg(KGlobal::locale()->formatMoney(totalSum)));
    ui_mainview.lblSubtotal->setText(QString("%1").arg(KGlobal::locale()->formatMoney(subTotalSum)));
    long double paid, change;
    bool isNum;
    paid = ui_mainview.editAmount->text().toDouble(&isNum);
    if (isNum) change = paid - totalSum; else change = 0.0;
    if (paid <= 0) change = 0.0;
    ui_mainview.labelChange->setText(QString("%1") .arg(KGlobal::locale()->formatMoney(change)));
    ui_mainview.lblSaleTaxes->setText(QString("%1") .arg(KGlobal::locale()->formatMoney(totalTax)));
    //update client discount
    QString dStr;
    if (clientInfo.discount >0) {
        dStr = i18n("Discount: %1%  [%2]",clientInfo.discount, KGlobal::locale()->formatMoney(discMoney));
    } else if (oDiscountMoney >0 ){
        dStr = i18n("Discount: %1", KGlobal::locale()->formatMoney(oDiscountMoney));
    }
    ui_mainview.lblClientDiscount->setText(dStr);
    
    //END of Rewrite
}

void lemonView::doEmitSignalQueryDb()
{
  emit signalQueryDb(ui_mainview.editItemCode->text());
}

bool lemonView::incrementTableItemQty(QString code, double q)
{
  double qty  = 1;
  double discount_old=0.0;
  double qty_old=0.0;
  double stockqty=0;
  bool done=false;
  ProductInfo info;

   if (productsHash.contains(code.toULongLong())) {
    //get product info...
    info = productsHash.value(code.toULongLong());

    stockqty = info.stockqty;
    qty = info.qtyOnList;
    qty_old = qty;
    QStringList itemsNotAvailable;
    
    //stock qty for groups are different.
    bool available = true;
    if (info.isAGroup) {
      Azahar *myDb = new Azahar;
      myDb->setDatabase(db);
      QStringList lelem = info.groupElementsStr.split(",");
      foreach(QString ea, lelem) {
        qulonglong c  = ea.section('/',0,0).toULongLong();
        double     qq = ea.section('/',1,1).toDouble();
        ProductInfo pi = myDb->getProductInfo(QString::number(c));
        QString unitStr;
        bool yes = false;
        double onList = getTotalQtyOnList(pi); // item itself and contained in any gruped product.
        // q     : item qty to add.
        // qq    : item qty on current grouped element to add
        // qq*q  : total items to add for this product.
        // onList: items of the same product already on the shopping list.
        if (pi.stockqty >= ((qq*q)+onList) ) yes = true;
        available = (available && yes );
        if (!yes) {
          itemsNotAvailable << i18n("%1 has %2 %3 but requested %4 + %5",pi.desc,pi.stockqty,unitStr,qq*q,onList);
        }
        qDebug()<<pi.desc<<" qtyonstock:"<<pi.stockqty<<" needed qty (onlist and new):"<<QString::number((qq*q)+onList);
      }
      delete myDb;
    } else {
      double onList = getTotalQtyOnList(info); // item itself and contained in any gruped product.
      if (stockqty >= q+onList) available = true; else available = false;
      qDebug()<<info.desc<<" qtyonstock:"<<info.stockqty<<" needed qty (onlist and new):"<<QString::number(q+onList);
    }
      
    if (available) qty+=q; else {
      QString msg;
      double onList = getTotalQtyOnList(info); // item itself and contained in any gruped product.
      if (!itemsNotAvailable.isEmpty())
        msg = i18n("<html><font color=red><b>The group/pack is not available because:<br>%1</b></font></html>", itemsNotAvailable.join("<br>"));
      else
        msg = i18n("<html><font color=red><b>There are only %1 articles of your choice at stock.<br> You requested %2</b></font></html>", info.stockqty,q+onList);
      
      if (ui_mainview.mainPanel->currentIndex() == pageMain) {
         tipCode->showTip(msg, 6000);
      }
      if (ui_mainview.mainPanel->currentIndex() == pageSearch) {
         ui_mainview.labelSearchMsg->setText(msg);
         ui_mainview.labelSearchMsg->show();
         QTimer::singleShot(3000, this, SLOT(clearLabelSearchMsg()) );
      }
    }
    QTableWidgetItem *itemQ = ui_mainview.tableWidget->item(info.row, colQty);//item qty
    itemQ->setData(Qt::EditRole, QVariant(qty));
    done = true;
    QTableWidgetItem *itemD = ui_mainview.tableWidget->item(info.row, colDisc);//item discount
    discount_old = itemD->data(Qt::DisplayRole).toDouble();
    //calculate new discount
    double discountperitem = (discount_old/qty_old);
    double newdiscount = discountperitem*qty;
    itemD->setData(Qt::EditRole, QVariant(newdiscount));
    //qDebug()<<"incrementTableQty... old discount:"<<discount_old<<" old qty:"<<qty_old<<" new discount:"<<newdiscount<<"new qty:"<<qty<<" disc per item:"<<discountperitem;

    info.qtyOnList = qty;
    //qDebug()<<"  New qty on list:"<<info.qtyOnList;
    productsHash.remove(code.toULongLong());
    productsHash.insert(info.code, info);

    //get item Due to update it.
    QTableWidgetItem *itemDue = ui_mainview.tableWidget->item(info.row, colDue); //4 item Due
    itemDue->setData(Qt::EditRole, QVariant((info.price*qty)-newdiscount));//fixed on april 30 2009 00:35. Added *qtyqDebug()<<"INCREMENTING TABLE ITEM QTY";
    refreshTotalLabel();
    QTableWidgetItem *item = ui_mainview.tableWidget->item(info.row, colCode);//item code
    displayItemInfo(item); //TODO: Cambiar para desplegar de ProductInfo.
    ui_mainview.editItemCode->clear();
   }//if productsHash.contains...

  return done;
}

void lemonView::insertItem(QString code)
{
  if ( code.isEmpty() || code == "0" || code.startsWith("0*") || code.endsWith("0.")) {
      ui_mainview.editItemCode->clear();
      return;
  }
  
  if ( !specialOrders.isEmpty() ) {
    KNotification *notify = new KNotification("information", this);
    notify->setText(i18n("Only Special Orders can be added. Please finish the current special order before adding any other product."));
    QPixmap pixmap = DesktopIcon("dialog-information",32);
    notify->setPixmap(pixmap);
    notify->sendEvent();
    ui_mainview.editItemCode->clear();
    return;
  }

qDebug()<< __FUNCTION__ <<" doNotAddMoreItems = "<<doNotAddMoreItems;
if ( doNotAddMoreItems ) { //only for reservations
    KNotification *notify = new KNotification("information", this);
    notify->setText(i18n("Cannot Add more items to the Reservation."));
    QPixmap pixmap = DesktopIcon("dialog-information",32);
    notify->setPixmap(pixmap);
    notify->sendEvent();
    ui_mainview.editItemCode->clear();
    return;
}
  
  double qty  = 1;
  QString codeX = code;
  ProductInfo info;
  info.code = 0;
  info.desc = "[INVALID]";

  //now code could contain number of items to insert,example:  10*1234567890
  QStringList list = code.split(QRegExp("[//*]{1,1}"),QString::SkipEmptyParts);
  if (list.count()==2) {
    qty =   list.takeAt(0).toDouble();
    codeX = list.takeAt(0);
  }

  //Here there are barcodes that support weight. Such products begin with a 2 and its length is 13.
    ///@link http://en.wikipedia.org/wiki/Universal_Product_Code#Prefixes
    qDebug()<<" EXAMINING PRODUCT CODE "<<codeX;
    if ( codeX.length() == 13 && codeX.startsWith("2") ) {
        ///This is a weighted product, such as meat and fruits...
        //get the weight, which is at positions RRRRRR of the code ( SLLLLLLMRRRRRRE ).
        QString pWeight = codeX.right(6);
        pWeight = pWeight.remove(5, 1);
        if (pWeight.length() == 5) {
            //add the decimal point...
            pWeight.insert(2, ".");
            qty = pWeight.toDouble(); //convert weight to QTY.
        }
        ///now, exclude the weight from the codeX. NOTE: Document THIS!
        codeX = codeX.left(7); // only the first 7 digits, the first digit is the prefix, which must be '2'.
        //NOTE: Here, the prefix is considered to be part of the product code.
        qDebug()<<"  New codeX:"<<codeX<<" weight:"<<pWeight<<" Double weight:"<<qty;
    }

  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  info = myDb->getProductInfo(codeX); //includes discount and validdiscount
  qDebug()<<" CodeX = "<<codeX<<" Numeric Code:"<<info.code<<" Alphacode:"<<info.alphaCode<<" Required Qty:"<<qty;
  delete myDb;

  //the next 'if' checks if the hash contains the product and got values from there.. To include purchaseQty, that we need!
  if (productsHash.contains( info.code )) 
      info = productsHash.value( info.code );

  QString msg;

  //verify item units and qty..
  if (info.units == uPiece) {
    unsigned int intqty = qty;
    if ( qty-intqty > 0 ) {
      //quantities are different, and its a fraction.. like 0.5 or 1.2
      msg = i18n("The requiered qty is %1 and its not allowed for the required product, please verify the quantity", qty);
      if (intqty <= 0)  //Because below there is a return if qty<=0
        tipCode->showTip(msg, 6000);
    }
    qty = intqty;
  }

  if ( qty <= 0) {return;}
  
  if (!incrementTableItemQty( QString::number(info.code) /*codeX*/, qty) ) {
    info.qtyOnList = qty;

    
    int insertedAtRow = -1;
    bool productFound = false;
    if ( info.code > 0 ) productFound = true;
    if ( info.isARawProduct ) productFound = false;
    double descuento=0.0;
    if (info.validDiscount) descuento = info.disc*qty;
    if ( !productFound )
      msg = i18n("<html><font color=red><b>Product not found in database.</b></font></html>");
    else if ( productFound ) {
      //NOW CHECK IF ITS A GROUP
      QString iname = info.desc;
      if (info.isAGroup ) {
        QStringList itemsNotAvailable;
        if (!info.groupElementsStr.isEmpty()) {
          Azahar *myDb = new Azahar;
          myDb->setDatabase(db);
          bool available = true;
          QStringList lelem = info.groupElementsStr.split(",");
          foreach(QString ea, lelem) {
            qulonglong c = ea.section('/',0,0).toULongLong();
            double     q = ea.section('/',1,1).toDouble();
            ProductInfo pi = myDb->getProductInfo(QString::number(c));
            QString unitStr;
            if (pi.units == 1 ) unitStr=" "; else unitStr = pi.unitStr;
            iname += '\n' + QString::number(q) + " "+ unitStr +" "+ pi.desc;
            bool yes = false;
            double onList = getTotalQtyOnList(pi); // item itself and contained in any gruped product.
            // q     : item qty to add.
            // qq    : item qty on current grouped element to add
            // q*qty : total items to add for this product.
            // onList: items of the same product already on the shopping list.
            if (pi.stockqty >= ((q*qty)+onList) ) yes = true;
            available = (available && yes );
            if (!yes) {
              itemsNotAvailable << i18n("%1 has %2 %3 but requested %4 + %5",pi.desc,pi.stockqty,unitStr,qty*q,onList);
            }
            qDebug()<<pi.desc<<" qtyonstock:"<<pi.stockqty<<" needed qty:"<<QString::number(qty*q);
          }
          //CHECK AVAILABILITY
          if (available || availabilityDoesNotMatters ) {
            if (availabilityDoesNotMatters) qDebug() << __FUNCTION__ <<" Availability DOES NOT MATTERS! ";
            insertedAtRow = doInsertItem( QString::number(info.code) /*codeX*/, iname, qty, info.price, descuento, info.unitStr);
          }
          else
            msg = i18n("<html><font color=red><b>The group/pack is not available because:<br>%1</b></font></html>", itemsNotAvailable.join("<br>"));
          delete myDb;
        }
      } else { //It is not a grouped product
        double onList = getTotalQtyOnList(info); // item itself and contained in any gruped product.
        if (info.stockqty >=  qty+onList || availabilityDoesNotMatters) {
          if (availabilityDoesNotMatters) qDebug() << __FUNCTION__ <<" Availability DOES NOT MATTERS! ";
          insertedAtRow = doInsertItem( QString::number(info.code) /*codeX*/, iname, qty, info.price, descuento, info.unitStr);
        }
        else
          msg = i18n("<html><font color=red><b>There are only %1 articles of your choice at stock.<br> You requested %2</b></font></html>", info.stockqty,qty+onList);
      }
    } else qDebug()<<"\n\n***Este ELSE no importa!!! ya se tomaron acciones al respecto***\nTHIS SHOULD NOT BE PRINTED!!!\n\n";
      
    if (!msg.isEmpty()) {
        if (ui_mainview.mainPanel->currentIndex() == pageMain) {
          tipCode->showTip(msg, 6000);
        }
        if (ui_mainview.mainPanel->currentIndex() == pageSearch) {
          ui_mainview.labelSearchMsg->setText(msg);
          ui_mainview.labelSearchMsg->show();
          QTimer::singleShot(3000, this, SLOT(clearLabelSearchMsg()) );
        }
    ui_mainview.editItemCode->clear();
    }
    info.row = insertedAtRow;
    if (info.row >-1 && info.desc != "[INVALID]" && info.code>0){
      productsHash.insert(info.code /*codeX.toULongLong()*/, info);
      QTableWidgetItem *item = ui_mainview.tableWidget->item(info.row, colCode);
      displayItemInfo(item);
      refreshTotalLabel();
    }
  }//if !increment...
  //Saving session.
  qDebug()<<"** INSERTING A PRODUCT [updating balance/transaction]";
  updateBalance(false);
  updateTransaction();
}//insertItem

double lemonView::getTotalQtyOnList(const ProductInfo &info)
{
  double result = 0;
  //first inspect the products hash to see how many direct products are there, then if any group contains
  foreach (ProductInfo pi, productsHash) {
    //first direct products.
    if (pi.code == info.code) {
      result += pi.qtyOnList;
      qDebug()<<"Found product "<<info.code<<" with "<<pi.qtyOnList<<" items in shopping list.";
    } else { //so its not the product itself, maybe its a group containing it
      if (pi.isAGroup) {
        QStringList lelem = pi.groupElementsStr.split(",");
        foreach(QString ea, lelem) {
          qulonglong c  = ea.section('/',0,0).toULongLong();
          double     qq = ea.section('/',1,1).toDouble();
          if (c == info.code) { //YES its contained in this group
            double qqq = qq*pi.qtyOnList;
            result += qqq;
            qDebug()<<" Found product "<<info.code<<" on grouped product "<<c<<" containing "<<qqq<<" items";
          }
        }
      }
    } //it was a group
  }
  return result;
}

void lemonView::updateItem(ProductInfo prod)
{
    QTableWidgetItem *item = ui_mainview.tableWidget->item(prod.row, colCode);
    displayItemInfo(item);

    //check if discounts
    double itemDiscount = 0;
    if (prod.disc > 0 || (prod.discpercentage > 0 && prod.validDiscount) ) {
        qDebug()<<"Item may have a discount.  Disc:"<<prod.disc<<" discPercent:"<<prod.discpercentage<<" valid Discount:"<<prod.validDiscount;
        if (prod.disc > 0 && !prod.validDiscount)
            itemDiscount += prod.disc; //price change
        if ( prod.discpercentage > 0 && prod.validDiscount )
            itemDiscount += (prod.discpercentage/100)*prod.qtyOnList; //item offer
        qDebug()<<"ItemDiscount:"<<itemDiscount;
    }
    //change color for discounts
    item  = ui_mainview.tableWidget->item(prod.row, colDisc);
    if (itemDiscount > 0) {
        QBrush b = QBrush(QColor::fromRgb(255,0,0), Qt::SolidPattern);
        item->setForeground(b);
    }
    item->setData(Qt::EditRole, QVariant(itemDiscount));
    //now update product total
    item = ui_mainview.tableWidget->item(prod.row, colDue);
    item->setData(Qt::EditRole, QVariant((prod.qtyOnList*prod.price)-itemDiscount));
    //set the color of the total if discounted.
    if (itemDiscount > 0) {
        QBrush b = QBrush(QColor::fromRgb(255,0,0), Qt::SolidPattern);
        item->setForeground(b);
    }

    ui_mainview.tableWidget->resizeRowsToContents();
}

int lemonView::doInsertItem(QString itemCode, QString itemDesc, double itemQty, double itemPrice, double itemDiscount, QString itemUnits)
{
  int rowCount = ui_mainview.tableWidget->rowCount();
  ui_mainview.tableWidget->insertRow(rowCount);
  ui_mainview.tableWidget->setItem(rowCount, colCode, new QTableWidgetItem(itemCode));
  ui_mainview.tableWidget->setItem(rowCount, colDesc, new QTableWidgetItem(itemDesc));
  ui_mainview.tableWidget->setItem(rowCount, colPrice, new QTableWidgetItem(""));//must be empty for HACK
  ui_mainview.tableWidget->setItem(rowCount, colQty, new QTableWidgetItem(QString::number(itemQty)));
  ui_mainview.tableWidget->setItem(rowCount, colUnits, new QTableWidgetItem(itemUnits));
  ui_mainview.tableWidget->setItem(rowCount, colDisc, new QTableWidgetItem(""));//must be empty for HACK
  ui_mainview.tableWidget->setItem(rowCount, colDue, new QTableWidgetItem(""));//must be empty for HACK

  QTableWidgetItem *item = ui_mainview.tableWidget->item(rowCount, colDisc);
  if (itemDiscount>0) {
    QBrush b = QBrush(QColor::fromRgb(255,0,0), Qt::SolidPattern);
    item->setForeground(b);
  }
  //HACK:The next 4 lines are for setting numbers with comas (1,234.00) instead of 1234.00.
  //      seems to be an effect of QVariant(double d)
  item->setData(Qt::EditRole, QVariant(itemDiscount));
  item = ui_mainview.tableWidget->item(rowCount, colDue);
  //item->setData(Qt::EditRole, QVariant(itemQty*(itemPrice-itemDiscount)));
  item->setData(Qt::EditRole, QVariant((itemQty*itemPrice)-itemDiscount)); //fixed on april 30 2009 00:35.
  item = ui_mainview.tableWidget->item(rowCount, colPrice);
  item->setData(Qt::EditRole, QVariant(itemPrice));

  //This resizes the heigh... looks beter...
  ui_mainview.tableWidget->resizeRowsToContents();

  if (productsHash.contains(itemCode.toULongLong())) { 
    ProductInfo  info = productsHash.value(itemCode.toULongLong());
    if (info.units != uPiece) itemDoubleClicked(item);//NOTE: Pieces must be id=1 at database!!!! its a workaround.
  }

  refreshTotalLabel();
  // BFB: editFilterbyDesc keeps the focus,
  if (!ui_mainview.editFilterByDesc->hasFocus())
    ui_mainview.editItemCode->setFocus();

  ui_mainview.editItemCode->setText("");
  ui_mainview.editItemCode->setCursorPosition(0);
  ui_mainview.mainPanel->setCurrentIndex(pageMain);

  return rowCount;
}

void lemonView::deleteSelectedItem()
{
  if (startingReservation || finishingReservation) {
      KNotification *notify = new KNotification("information", this);
      notify->setText(i18n("Cannot delete items from a reservation."));
      QPixmap pixmap = DesktopIcon("dialog-information",32);
      notify->setPixmap(pixmap);
      notify->sendEvent();
      return;     
  }
  
  bool continueIt=false;
  bool reinsert = false;
  double qty=0;
  if (ui_mainview.tableWidget->currentRow()!=-1 && ui_mainview.tableWidget->selectedItems().count()>4) {
    if ( !Settings::lowSecurityMode() ) {
        if (Settings::requiereDelAuth() ) {
          dlgPassword->show();
          dlgPassword->hide();
          dlgPassword->clearLines();
          if ( dlgPassword->exec() ) continueIt=true;
        } else continueIt=true; //if requiereDelAuth
    } else continueIt=true; //if no low security

    if (continueIt) {
      int row = ui_mainview.tableWidget->currentRow();
      QTableWidgetItem *item = ui_mainview.tableWidget->item(row, colCode);
      QString codeStr = item->data(Qt::DisplayRole).toString();

      if ( codeStr.toULongLong() == 0 ) {
        //its not a product, its a s.o.
        codeStr.remove(0,3); //remove the "so." string
        qulonglong id = codeStr.toULongLong();
        if (specialOrders.contains(id)) {
          SpecialOrderInfo info = specialOrders.take(id);
          //check if is completing the order
          if (info.status == stReady) { //yes, its completing the order, but wants to cancel the action.
            //remove from listview
            ui_mainview.tableWidget->removeRow(row);
            ui_mainview.editItemCode->setFocus();
            if (ui_mainview.tableWidget->rowCount() == 0) ui_mainview.comboClients->setEnabled(true);
            refreshTotalLabel();
            qDebug()<<" Removing a SO when completing the Order";
            return;
          }
          if ( info.qty == 1 ) {
            Azahar *myDb = new Azahar;
            myDb->setDatabase(db);
            myDb->deleteSpecialOrder(id);
            //remove from listview
            ui_mainview.tableWidget->removeRow(row);
            QString authBy = dlgPassword->username();
            if (authBy.isEmpty()) authBy = myDb->getUserName(1); //default admin.
            log(loggedUserId, QDate::currentDate(), QTime::currentTime(), i18n("Removing an Special Item from shopping list. Authorized by %1",authBy));
            if (ui_mainview.tableWidget->rowCount() == 0) ui_mainview.comboClients->setEnabled(true);
            ui_mainview.editItemCode->setFocus();
            refreshTotalLabel();
            delete myDb;
            return;
          }
          //more than one
          double iqty = info.qty-1;
          info.qty = iqty;
          double newdiscount = info.disc * info.payment * iqty;
          
          item = ui_mainview.tableWidget->item(row, colQty);
          item->setData(Qt::EditRole, QVariant(iqty));
          item = ui_mainview.tableWidget->item(row, colDue);
          item->setData(Qt::EditRole, QVariant((iqty*info.payment)-newdiscount));
          item = ui_mainview.tableWidget->item(row, colDisc);
          item->setData(Qt::EditRole, QVariant(newdiscount));
          
          //reinsert to the hash
          specialOrders.insert(info.orderid,info);
        }
        if (ui_mainview.tableWidget->rowCount() == 0) ui_mainview.comboClients->setEnabled(true);
        ui_mainview.editItemCode->setFocus();
        refreshTotalLabel();
        return; //to exit the method, we dont need to continue.
      }
      
      qulonglong code = item->data(Qt::DisplayRole).toULongLong();
      ProductInfo info = productsHash.take(code); //insert it later...
      qty = info.qtyOnList; //this must be the same as obtaining from the table... this arrived on Dec 18 2007
      //if the itemQty is more than 1, decrement it, if its 1, delete it
      item = ui_mainview.tableWidget->item(row, colUnits);//get item Units in strings...
      QString iUnitString = item->data(Qt::DisplayRole).toString();
      item = ui_mainview.tableWidget->item(row, colQty); //get Qty
      if ((item->data(Qt::DisplayRole).canConvert(QVariant::Double))) {
        qty = item->data(Qt::DisplayRole).toDouble();
       //NOTE:
       //  Here, we are going to delete only items that are bigger than 1. and remove them one by one..
       //  or are we goint to decrement items only sold by pieces?
        if (qty>1 && info.units==uPiece) {
          qty--;
          item->setData(Qt::EditRole, QVariant(qty));
          double price    = info.price;
          double discountperitem = info.disc;
          double newdiscount = discountperitem*qty;
          item = ui_mainview.tableWidget->item(row, colDue);
          item->setData(Qt::EditRole, QVariant((qty*price)-newdiscount));
          item = ui_mainview.tableWidget->item(row, colDisc);
          item->setData(Qt::EditRole, QVariant(newdiscount));
          info.qtyOnList = qty;
          reinsert = true;
        }//if qty>1
        else { //Remove from the productsHash and tableWidget...
          //get item code
          //int removed = productsHash.remove(code);
          productsHash.remove(code);
          ui_mainview.tableWidget->removeRow(row);
          reinsert = false;
        }//qty = 1...
      }//if canConvert
      if (reinsert) productsHash.insert(code, info); //we remove it with .take...
      Azahar *myDb = new Azahar;
      myDb->setDatabase(db);
      QString authBy = dlgPassword->username();
      if (authBy.isEmpty()) authBy = myDb->getUserName(1); //default admin.
      log(loggedUserId, QDate::currentDate(), QTime::currentTime(), i18n("Removing an article from shopping list. Authorized by %1", authBy));
      delete myDb;

       qDebug()<<"** REMOVING A PRODUCT [updating balance/transaction]";
       updateBalance(false);
       updateTransaction();
       
    }//continueIt
  }//there is something to delete..

  if (ui_mainview.tableWidget->rowCount() == 0) ui_mainview.comboClients->setEnabled(true);
  refreshTotalLabel();
}

void lemonView::itemDoubleClicked(QTableWidgetItem* item)
{
  int row = item->row();
  double dqty = 0.0;
  bool   ok   = false;
  int    iqty = 0;
  
  QTableWidgetItem *i2Modify = ui_mainview.tableWidget->item(row, colCode);
  qulonglong code = i2Modify->data(Qt::DisplayRole).toULongLong();
  if (!productsHash.contains(code)) {
    //its not a product, its a s.o.
    QString oid = i2Modify->data(Qt::DisplayRole).toString();
    oid.remove(0,3);
    qulonglong id = oid.toULongLong();
    if (specialOrders.contains(id)) {
      SpecialOrderInfo info = specialOrders.take(id);
      //check if is completing the order
      if (info.status == stReady) return; //is completing the order, cant modify qty.

      iqty = info.qty+1;
      info.qty = iqty;
      double newdiscount = info.disc * info.payment * iqty;

      i2Modify = ui_mainview.tableWidget->item(row, colQty);
      i2Modify->setData(Qt::EditRole, QVariant(iqty));
      i2Modify = ui_mainview.tableWidget->item(row, colDue);
      i2Modify->setData(Qt::EditRole, QVariant((iqty*info.payment)-newdiscount));
      i2Modify = ui_mainview.tableWidget->item(row, colDisc);
      i2Modify->setData(Qt::EditRole, QVariant(newdiscount));

      //reinsert to the hash
      specialOrders.insert(info.orderid,info);
    }
    ui_mainview.editItemCode->setFocus();
    refreshTotalLabel();
    return; //to exit the method, we dont need to continue.
  }
  
  ProductInfo info = productsHash.take(code);
  double dmaxItems = info.stockqty;
  QString msg = i18n("Enter the number of %1", info.unitStr); //Added on Dec 15, 2007

  //Launch a dialog to as the new qty
  if (info.units == uPiece) {
    if (dmaxItems > 0) {
      ok = true;
      iqty = info.qtyOnList+1;
      //NOTE: Present a dialog to enter a qty or increment by one ?
    }
  }
  else {
    ///FIXME: Alert the user why is restricted to a max items!
    InputDialog *dlg = new InputDialog(this, false, dialogMeasures, msg, 0.001, dmaxItems);
    if (dlg->exec() ) {
      dqty = dlg->dValue;
      ok=true;
    }
  }
  if (ok) {
    double newqty = dqty+iqty; //one must be zero
    //modify Qty and discount...
    i2Modify = ui_mainview.tableWidget->item(row, colQty);
    i2Modify->setData(Qt::EditRole, QVariant(newqty));
    double price    = info.price;
    double discountperitem = info.disc;
    double newdiscount = discountperitem*newqty;
    i2Modify = ui_mainview.tableWidget->item(row, colDue);
    i2Modify->setData(Qt::EditRole, QVariant((newqty*price)-newdiscount));
    i2Modify = ui_mainview.tableWidget->item(row, colDisc);
    i2Modify->setData(Qt::EditRole, QVariant(newdiscount));
    info.qtyOnList = newqty;

    ui_mainview.editItemCode->setFocus();
  } else {
    msg = i18n("<html><font color=red><b>Product not available in stock for the requested quantity.</b></font></html>");
    tipCode->showTip(msg, 6000);
  }
  productsHash.insert(code, info);
  refreshTotalLabel();
}

void lemonView::itemSearchDoubleClicked(QTableWidgetItem *item)
{
  int row = item->row();
  QTableWidgetItem *cItem = ui_mainview.tableSearch->item(row,2); //get item code
  qulonglong code = cItem->data(Qt::DisplayRole).toULongLong();
  //qDebug()<<"Linea 981: Data at column 2:"<<cItem->data(Qt::DisplayRole).toString();
  if (productsHash.contains(code)) {
    int pos = getItemRow(QString::number(code));
    if (pos>=0) {
      QTableWidgetItem *thisItem = ui_mainview.tableWidget->item(pos, colCode);
      ProductInfo info = productsHash.value(code);
      if (info.units == uPiece) incrementTableItemQty(QString::number(code), 1);
      else itemDoubleClicked(thisItem);
    }
  }
  else {
    insertItem(QString::number(code));
  }
  ui_mainview.mainPanel->setCurrentIndex(pageMain);
}

void lemonView::displayItemInfo(QTableWidgetItem* item)
{
  int row = item->row();
  qulonglong code  = (ui_mainview.tableWidget->item(row, colCode))->data(Qt::DisplayRole).toULongLong();
  QString desc  = (ui_mainview.tableWidget->item(row, colDesc))->data(Qt::DisplayRole).toString();
  double price = (ui_mainview.tableWidget->item(row, colPrice))->data(Qt::DisplayRole).toDouble();
  if (productsHash.contains(code)) {
    ProductInfo info = productsHash.value(code);
    QString uLabel=info.unitStr; // Dec 15  2007

    double discP=0.0;
    if (info.validDiscount) discP = info.discpercentage;
    QString str;
    QString tTotalTax= i18n("Taxes:");
    QString tTax    = i18n("Tax:");
    QString tOTax   = i18n("Other taxes:");
    QString tUnits  = i18n("Sold by:");
    QString tPrice  = i18n("Price:");
    QString tDisc   = i18n("Discount:");
    QString tPoints = i18n("Points:");
    double pWOtax = 0;
    if (Settings::addTax()) //added on jan 28 2010
      pWOtax = info.price;
    else
      pWOtax= info.price/(1+((info.tax+info.extratax)/100)); //This is not 100% exact.
    double tax1m = (info.tax/100)*pWOtax;
    double tax2m = (info.extratax/100)*pWOtax;
    info.totaltax = tax1m + tax2m;
    QPixmap pix;
    pix.loadFromData(info.photo);

    ui_mainview.labelDetailPhoto->setPixmap(pix);
    str = QString("%1 (%2 %)")
        .arg(KGlobal::locale()->formatMoney(info.totaltax)).arg(info.tax+info.extratax);
    ui_mainview.labelDetailTotalTaxes->setText(QString("<html>%1 <b>%2</b></html>")
        .arg(tTotalTax).arg(str));
    str = QString("%1 (%2 %)")
        .arg(KGlobal::locale()->formatMoney(tax1m)).arg(info.tax);
    ui_mainview.labelDetailTax1->setText(QString("<html>%1 <b>%2</b></html>")
        .arg(tTax).arg(str));
    str = QString("%1 (%2 %)")
        .arg(KGlobal::locale()->formatMoney(tax2m)).arg(info.extratax);
    ui_mainview.labelDetailTax2->setText(QString("<html>%1 <b>%2</b></html>")
        .arg(tOTax).arg(str));
    ui_mainview.labelDetailUnits->setText(QString("<html>%1 <b>%2</b></html>")
        .arg(tUnits).arg(uLabel));
    ui_mainview.labelDetailDesc->setText(QString("<html><b>%1</b></html>").arg(desc));
    ui_mainview.labelDetailPrice->setText(QString("<html>%1 <b>%2</b></html>")
        .arg(tPrice).arg(KGlobal::locale()->formatMoney(price)));
    ui_mainview.labelDetailDiscount->setText(QString("<html>%1 <b>%2 (%3 %)</b></html>")
        .arg(tDisc).arg(KGlobal::locale()->formatMoney(info.disc)).arg(discP));
    if (info.points>0) {
      ui_mainview.labelDetailPoints->setText(QString("<html>%1 <b>%2</b></html>")
        .arg(tPoints).arg(info.points));
      ui_mainview.labelDetailPoints->show();
    } else ui_mainview.labelDetailPoints->hide();
  }
}

/* TRANSACTIONS ZONE */
/*------------------*/

QString lemonView::getCurrentTransactionString()
{
  return QString::number(currentTransaction);
}

qulonglong  lemonView::getCurrentTransaction()
{
  return currentTransaction;
}

void lemonView::createNewTransaction(TransactionType type)
{
  //If there is an operation started, doit...
  if ( operationStarted ) {
    TransactionInfo info;
    info.type = type;
    info.amount = 0;
    info.date   = QDate::currentDate();
    info.time   = QTime::currentTime();
    info.paywith= 0;
    info.changegiven =0;
    info.paymethod = pCash;
    info.state = tNotCompleted;
    info.userid = loggedUserId;
    info.clientid = clientInfo.id;
    info.cardnumber ="-NA-";
    info.cardauthnum="-NA-";
    info.itemcount= 0;
    info.itemlist = "";
    info.disc = 0;
    info.discmoney = 0;
    info.points = 0;
    info.utility = 0;
    //info.groups = "";
    info.providerid = 1; //default one... for no.. FIXME!
    info.terminalnum=Settings::editTerminalNumber();
    info.balanceId = currentBalanceId;

    Azahar *myDb = new Azahar;
    myDb->setDatabase(db);
    currentTransaction = myDb->insertTransaction(info);
    qDebug()<<"NEW TRANSACTION:"<<currentTransaction;
    if (currentTransaction <= 0) {
      KMessageBox::detailedError(this, i18n("Lemon has encountered an error when openning database, click details to see the error details."), myDb->lastError(), i18n("Create New Transaction: Error"));
    }
    else {
      transactionInProgress = true;
      emit signalUpdateTransactionInfo();
    }
    delete myDb;
   }
  productsHash.clear();
  specialOrders.clear();
}

void lemonView::finishCurrentTransaction()
{
  bool canfinish = true;
  completingOrder = false; //reset flag..
  TicketInfo ticket;
  
  refreshTotalLabel();
  QString msg;
  ui_mainview.mainPanel->setCurrentIndex(pageMain);
  if (ui_mainview.editAmount->text().isEmpty()) ui_mainview.editAmount->setText("0.0");
  if (ui_mainview.checkCash->isChecked()) {
    double amnt = ui_mainview.editAmount->text().toDouble();


    ///NOTE:The bug that does not accept exact amount to pay, came in recent time. It was not present some time ago.
    ///     Maybe it is due to some system|library software update (change).
    double dif1 = amnt - totalSum;
    double dif2 = totalSum - amnt;

    //qDebug()<<"  dif1    :"<<QString::number(dif1,'f',64);
    //qDebug()<<"  dif1    :"<<QString::number(dif2,'f',64);
    qDebug()<<"  amnt    :"<<QString::number(amnt,'f',64);
    qDebug()<<"  totalSum:"<<QString::number(totalSum,'f',64);

    bool iguales = false;
    if ( (dif1 < 0.000001) && (dif2 < 0.000001) ) {
        iguales = true;//the difference is practically zero.
    }

    if (iguales)
        canfinish = true;
    else if ( amnt < totalSum ) {
      canfinish = false;
      ui_mainview.editAmount->setFocus();
      ui_mainview.editAmount->setStyleSheet("background-color: rgb(255,100,0); color:white; selection-color: white; font-weight:bold;");
      ui_mainview.editCardNumber->setStyleSheet("");
      ui_mainview.editAmount->setSelection(0, ui_mainview.editAmount->text().length());
      msg = i18n("<html><font color=red><b>Please fill the correct payment amount before finishing a transaction.</b></font></html>");
      tipAmount->showTip(msg, 4000);
    } else if (ui_mainview.editAmount->text().length() >= 8 )  {
      if (ui_mainview.editAmount->text().contains(".00") || ui_mainview.editAmount->text().contains(",00"))
        canfinish = true; // it was not entered by the barcode reader.
        //To continue with that big number, the cashier needs to enter a .00 or ,00 at the end of the amnt.
      else {
        // This can be an EAN8/EAN13 barcode introduced in the amount field!
        // There are reports of users doing this, leading to a bad balance.
        msg = i18n("Please be sure to enter an appropiate amount in this field. The number seems to be a barcode entered by mistake in the amount field.");
        tipAmount->showTip(msg, 10000); //let stay more time than other msg.
        canfinish = false;
        ui_mainview.editAmount->setSelection(0, ui_mainview.editAmount->text().length()); //NOTE: THIS LINE OF CODE IS THE 16,000th WRITTEN LINE!!!! According to SLOCCOUNT. MCH december 25 2009.
      }
    }
  }
  else {
    QString cn =  ui_mainview.editCardNumber->text();
    QString cna = ui_mainview.editCardAuthNumber->text();
    if (!ui_mainview.editCardNumber->hasAcceptableInput() || cn.isEmpty() || cn == "---") {
      canfinish = false;
      ui_mainview.editCardNumber->setFocus();
      ui_mainview.editCardNumber->setStyleSheet("background-color: rgb(255,100,0); color:white; font-weight:bold; selection-color: white;");
      ui_mainview.editAmount->setStyleSheet("");
      ui_mainview.editCardNumber->setSelection(0, ui_mainview.editCardNumber->text().length());
      msg = i18n("<html><font color=red><b>Please enter the card number.</b></font></html>");
    }
    else if (!ui_mainview.editCardAuthNumber->hasAcceptableInput() || cna.isEmpty() || cna.length()<4) {
      canfinish = false;
      ui_mainview.editCardAuthNumber->setFocus();
      ui_mainview.editCardAuthNumber->setStyleSheet("background-color: rgb(255,100,0); color:white; font-weight:bold; selection-color: white;");
      ui_mainview.editAmount->setStyleSheet("");
      ui_mainview.editCardAuthNumber->setSelection(0, ui_mainview.editCardAuthNumber->text().length());
      msg = i18n("<html><font color=red><b>Please enter the Authorisation number from the bank voucher.</b></font></html>");
    }
    if (!msg.isEmpty())
      tipAmount->showTip(msg, 4000);
  }
  if (ui_mainview.tableWidget->rowCount() == 0) canfinish = false;
  if (!canStartSelling()) {
    canfinish=false;
    KMessageBox::sorry(this, i18n("Before selling, you must start operations."));
  }

  if (canfinish) // Ticket #52: Allow ZERO DUE.
  {
    ui_mainview.editAmount->setStyleSheet("");
    ui_mainview.editCardNumber->setStyleSheet("");
    TransactionInfo tInfo;
    PaymentType      pType;
    double           payWith = 0.0;
    double           payTotal = 0.0;
    double           changeGiven = 0.0;
    QString          authnumber = "";
    QString          cardNum = "";
    QString          paidStr = "'[Not Available]'";
    QStringList      groupList;
    
    payTotal = totalSum;
    if (ui_mainview.checkCash->isChecked()) {
      pType = pCash;
      if (!ui_mainview.editAmount->text().isEmpty()) payWith = ui_mainview.editAmount->text().toDouble();
      changeGiven = payWith- totalSum;
    } else {
      pType = pCard;
      if (ui_mainview.editCardNumber->hasAcceptableInput() ) {
        cardNum = ui_mainview.editCardNumber->text().replace(0,15,"***************"); //FIXED: Only save last 4 digits;
      }
      if (ui_mainview.editCardAuthNumber->hasAcceptableInput())
        authnumber = ui_mainview.editCardAuthNumber->text();
      payWith = payTotal;
    }

    tInfo.id = currentTransaction;
    tInfo.balanceId = currentBalanceId;
    tInfo.type = 0;//already on db.

    qDebug()<<"FinishingReservation:"<<finishingReservation<<" reservation Payment:"<<reservationPayment;
    if (finishingReservation) 
        tInfo.amount = totalSum + reservationPayment; //at the transaction the REAL total must be considered (for accountability)
    else
        tInfo.amount = totalSum;

    //new feature from biel : Change sale date time
    bool printDTticket=true;
    if (!ui_mainview.groupSaleDate->isHidden()) { //not hidden, change date.
      QDateTime datetime = ui_mainview.editTransactionDate->dateTime();
      tInfo.date   =  datetime.date();
      tInfo.time   =  datetime.time();
      ticket.datetime = datetime;
      if (!Settings::printChangedDateTicket()) printDTticket = false;
    } else  { // hidden, keep current date as sale date.
      tInfo.date   = QDate::currentDate();
      tInfo.time   = QTime::currentTime();
      ticket.datetime = QDateTime::currentDateTime();
    }
    
    tInfo.paywith= payWith;
    tInfo.changegiven =changeGiven;
    tInfo.paymethod = pType;
    tInfo.state = tCompleted;
    tInfo.userid = loggedUserId;
    tInfo.clientid = clientInfo.id;
    tInfo.cardnumber = cardNum;
    tInfo.cardauthnum= authnumber;
    tInfo.itemcount= 0;//later
    tInfo.itemlist = ""; //at the for..
    tInfo.disc = clientInfo.discount;
    tInfo.discmoney = discMoney; //global variable... Now included the oDiscountMoney
    qDebug()<<"tInfo.disc:"<<tInfo.disc; qDebug()<<" tInfo.discmoney:"<<tInfo.discmoney;
    tInfo.points = buyPoints; //global variable...
    tInfo.utility = 0; //later
    tInfo.terminalnum=Settings::editTerminalNumber();
    tInfo.providerid = 1; //default... at sale we dont use providers.
    tInfo.totalTax = totalTax;

    QStringList productIDs; productIDs.clear();
    int cantidad=0;
    double utilidad=0;
    double soGTotal=0; //totals for all so.
    QDateTime soDeliveryDT;

    Azahar *myDb = new Azahar;
    myDb->setDatabase(db);

    if (finishingReservation) {
        //set reservation status to rCompleted.
        myDb->setReservationStatus(reservationId, rCompleted);
        //Add the reservation details to the ticket
        ticket.isAReservation = true;
        ticket.reservationStarted = false;
        ticket.reservationPayment = reservationPayment;
        ticket.purchaseTotal = myDb->getReservationTotalAmount(reservationId);
        qDebug()<<"*** Finishing RESERVATION ID:"<<reservationId<< " Purchase Total:"<<ticket.purchaseTotal<< " With a Payment of:"<<reservationPayment;
    } else if (startingReservation) {
        ticket.isAReservation     = true;
        ticket.reservationStarted = true;
        qDebug()<<"*** STARTING RESERVATION ID:"<<reservationId;
        //No deberia entrar aqui, porque al iniciar apartados, no termina transaccion.
    } else {
        ticket.isAReservation     = false;
        ticket.reservationStarted = false;
        ticket.reservationId = 0;
        qDebug()<<" >>>>>>>>>>>>>>>>>>>>>>>>>>>>> OK no reservation <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<" ;
    }


    QHashIterator<qulonglong, ProductInfo> i(productsHash);
    int position=0;
    QList<TicketLineInfo> ticketLines;
    ticketLines.clear();
    TransactionItemInfo tItemInfo;
    double pDiscounts=0;

    // NOTE: utilidad (profit): Also take into account client discount! after this...
    //Iterating products hash
    while (i.hasNext()) {
      QString iname = "";
      i.next();
      position++;
      productIDs.append(QString::number(i.key())+"/"+QString::number(i.value().qtyOnList));
      if (i.value().units == uPiece) cantidad += i.value().qtyOnList; else cantidad += 1; // :)
      utilidad  += (i.value().price - i.value().cost - i.value().disc) * i.value().qtyOnList;
      pDiscounts+= i.value().disc * i.value().qtyOnList;
      //decrement stock qty, increment soldunits.. CHECK FOR GROUP
      if (!i.value().isAGroup)
        myDb->decrementProductStock(i.key(), i.value().qtyOnList, QDate::currentDate() );
      else { //ITS A GROUPED PRODUCT
        myDb->decrementGroupStock(i.key(), i.value().qtyOnList, QDate::currentDate() );
        //GET NAME WITH ITS PRODUCTS
        iname = i.value().desc; //name with its components
        if (!i.value().groupElementsStr.isEmpty()) {
          QStringList lelem = i.value().groupElementsStr.split(",");
          //groupList << lelem;//to store on TransactionInfo
          foreach(QString ea, lelem) {
            if (Settings::printPackContents()) {
              qulonglong c = ea.section('/',0,0).toULongLong();
              double     q = ea.section('/',1,1).toDouble();
              ProductInfo pi = myDb->getProductInfo(QString::number(c));
              QString unitStr;
              if (pi.units == 1 ) unitStr=" "; else unitStr = pi.unitStr;
              iname += "\n  " + QString::number(q) + " "+ unitStr +" "+ pi.desc;
            }
          }
        }
      }

      //qDebug()<<"NEWNAME:"<<iname;

      //from Biel
      // save transactionItem
      tItemInfo.transactionid   = tInfo.id;
      tItemInfo.position        = position;
      tItemInfo.productCode     = i.key();
      tItemInfo.points          = i.value().points; // qtyOnList; //MCH: changed...
      tItemInfo.unitStr         = i.value().unitStr;
      tItemInfo.qty             = i.value().qtyOnList;
      tItemInfo.cost            = i.value().cost;
      tItemInfo.price           = i.value().price;
      tItemInfo.disc            = i.value().disc*i.value().qtyOnList;
      tItemInfo.total           = (i.value().price - i.value().disc) * i.value().qtyOnList;
      if (i.value().isAGroup)
        tItemInfo.name            = iname.replace("\n", "|");
      else
        tItemInfo.name            = i.value().desc;
      tItemInfo.soId            = ""; //no special order
      tItemInfo.payment         = 0; //not used
      tItemInfo.completePayment = true;
      tItemInfo.isGroup = i.value().isAGroup;
      tItemInfo.tax = (i.value().tax + i.value().extratax); //tax percentage (15 == 15%), as it was originally planed.
      //NOTE: Apr 14 2011: I decided to print the tax % in product iteration in tickets. Instead of the money.
      //                   This way, i dont need to fix the tax money amount if the item has discounts or the sale has a client/ocassional discount.
      //tItemInfo.tax = i.value().totaltax*i.value().qtyOnList; //((i.value().tax + i.value().extratax)/100)*tItemInfo.total;

      myDb->insertTransactionItem(tItemInfo);

      //re-select the transactionItems model
      historyTicketsModel->select();

      iname = iname.replace("\n", "|");

      // add line to ticketLines 
      TicketLineInfo tLineInfo;
      tLineInfo.qty     = i.value().qtyOnList;
      tLineInfo.unitStr = i.value().unitStr;
      tLineInfo.isGroup = false;
      if (i.value().isAGroup) { //TO SEND GROUP INFO TO PRINTTICKET
        tLineInfo.geForPrint    =iname;
        tLineInfo.completePayment = true;
        tLineInfo.payment = 0;
        tLineInfo.isGroup = true;
      }
      tLineInfo.desc    = i.value().desc;
      tLineInfo.price   = i.value().price;
      tLineInfo.disc    = i.value().disc*i.value().qtyOnList;
      tLineInfo.total   = tItemInfo.total;
      tLineInfo.tax     = tItemInfo.tax;
      //tLineInfo.geForPrint = 
      ticketLines.append(tLineInfo);
    } //each product on productHash
    
    tInfo.itemcount = cantidad; // qty of products (again, at Hash)

    double soDiscounts = 0;
    QStringList ordersStr;
    int completePayments = 0;
    //Now check the Special Items (Orders)
    if (!specialOrders.isEmpty()) {
      QStringList elementsStr;
      foreach(SpecialOrderInfo siInfo, specialOrders) {
        // NOTE: here the Special Item is taken as ONE -not counting its components- but COUNT QTY of each SO.
        tInfo.itemcount += siInfo.qty; //specialOrders.count();
        //Decrement each component stock!
        myDb->decrementSOStock(siInfo.orderid, siInfo.qty, QDate::currentDate());
        position++; //increment the existent positions.
        ordersStr.append(QString::number(siInfo.orderid)+"/"+QString::number(siInfo.qty));
        elementsStr.append(siInfo.groupElements);
        //NOTE: Here the 'utilidad' = profit. Profit is CERO at this stage for the S.O,
        //      Its going to be calculated when the payment is done (when picking up the product)
        //      and is going to be emited other transaction with the profit/payment.
        //      TODO: Add this note to the manual.

        if (siInfo.status == stReady) {
          if ( siInfo.completePayment ) {
            //the Special Order is being completed... CALCULATE PROFIT.
            double discount =  (siInfo.price * siInfo.disc * siInfo.qty) + lastDiscount;
            qDebug()<<"LAST DISCOUNT:"<<lastDiscount;
            utilidad += ((siInfo.price - siInfo.cost)*siInfo.qty) - discount;
            qDebug()<<" Profit for the SO:"<<((siInfo.price - siInfo.cost)*siInfo.qty)-discount<<"discount here:"<<(siInfo.price * siInfo.disc * siInfo.qty)<<" All Discount on the SO:"<<discount;
            //NOTE: Disconunts on any SO content is taken into consideration when adding to the transaction,
            //      so the siInfo.price that comes from the SO Editor does NOT include discounts.
            //      ALSO the price and cost is for ONE PACK. If incrementing/decrementing at lemon or at
            //      the SpecialOrderEditor (qty for the Order, not components), the siInfo.qty will contain this
            //      quantity, to be multiplied by the price and cost to get the TOTAL SALE cost and price for each SO.
          }
        }
        
        if (siInfo.units == 1) cantidad += siInfo.qty; else cantidad +=1;
        //from Biel
        // save transactionItem
        tItemInfo.disc = 0;
        tItemInfo.transactionid   = tInfo.id;
        tItemInfo.position        = position;
        tItemInfo.productCode     = 0; //are qulonlong... and they are not normal products
        tItemInfo.points          = 0;
        tItemInfo.unitStr         = siInfo.unitStr;
        tItemInfo.qty             = siInfo.qty;
        tItemInfo.cost            = siInfo.cost;
        tItemInfo.price           = siInfo.price;
        tItemInfo.disc            = siInfo.disc * siInfo.price * siInfo.qty; //this is the total discount
        soDiscounts              += tItemInfo.disc;
        double disc2              = siInfo.disc * siInfo.payment * siInfo.qty; //this is the discount on the prepayment
        double taxPercentage      = (myDb->getSpecialOrderAverageTax(siInfo.orderid));
        double taxmoney           = myDb->getSpecialOrderAverageTax(siInfo.orderid, rtMoney)*siInfo.qty; // tax per qty (still needs to be multiplied by qty)
        tItemInfo.total           = (siInfo.price*siInfo.qty)-tItemInfo.disc;
        //NOTE: Apr 14 2011: I decided to print the tax % in product iteration in tickets. Instead of the money.
        //                   This way, i dont need to fix the tax money amount if the item has discounts or the sale has a client/ocassional discount.
        tItemInfo.tax             = taxPercentage; //taxmoney;
        tItemInfo.name            = siInfo.name;
        tItemInfo.soId            = "so."+QString::number(siInfo.orderid);
        double sumTax =0;
        if (Settings::addTax()) sumTax=taxmoney;
        tItemInfo.payment         = (siInfo.payment*siInfo.qty) -disc2 + sumTax; //(taxPercentage*((siInfo.payment*siInfo.qty)-disc2));
        //qDebug()<<"ItemInfo taxes:"<<taxmoney;
        tItemInfo.completePayment = siInfo.completePayment;
        tItemInfo.deliveryDateTime= siInfo.deliveryDateTime;
        tItemInfo.isGroup         = false;

        if (siInfo.completePayment && siInfo.status == stReady) completePayments++;
        
        myDb->insertTransactionItem(tItemInfo);
        //re-select the transactionItems model
        historyTicketsModel->select();
        // add line to ticketLines
        TicketLineInfo tLineInfo;
        tLineInfo.disc = 0;
        tLineInfo.qty     = siInfo.qty;
        tLineInfo.unitStr = siInfo.unitStr;
        tLineInfo.desc    = siInfo.name;
        tLineInfo.price   = siInfo.price;
        tLineInfo.disc    = siInfo.disc * siInfo.price * siInfo.qty; // april 5 2005: Now SO can have discounts
        tLineInfo.partialDisc =disc2;
        tLineInfo.total   = tItemInfo.total;
        double gtotal     = tItemInfo.total + tItemInfo.tax; 
        tLineInfo.gtotal  =  Settings::addTax()  ? gtotal : tLineInfo.total;
        soGTotal         += tLineInfo.gtotal;
        soDeliveryDT      = siInfo.deliveryDateTime; // this will be the same for all the SO, so it does not matter if overwrited.
        tLineInfo.geForPrint = siInfo.geForPrint;
        tLineInfo.completePayment = siInfo.completePayment;
        tLineInfo.payment = tItemInfo.payment;
        tLineInfo.isGroup = false;
        tLineInfo.deliveryDateTime = siInfo.deliveryDateTime;
        tLineInfo.tax     = tItemInfo.tax;
        //qDebug()<<" \n==== total:"<<tLineInfo.total<<" Payment:"<< tLineInfo.payment<<" siInfo.payment:"<<siInfo.payment
        //<<" pDisc:"<<disc2<<" % tax $"<<tItemInfo.tax<<" Gran Total:"<<tLineInfo.gtotal<<"\n";
        ///NOTE: Testing with addTax setting and using a sample SO, there is a DIFFERENCE of 18 cents ( the client pays 18 cents less of the real price)
        ///      (REAL PRICE = 285.18, PAID: 285 ). This is the result of the 'rounding' in multiple operations done during the process.
        ///      The error is 0.063 % (285.18 * .00063 = .18)

        ticketLines.append(tLineInfo);

        switch (siInfo.status) {
          case stPending:  
            siInfo.status = stInProgress;
            //some clients makes the total payment when ordering.
            if (siInfo.completePayment)
              siInfo.completedOnTrNum = tInfo.id;
            myDb->updateSpecialOrder(siInfo);
            break;
          case stInProgress: 
            qDebug()<<"There is an inappropiate state (In progress) for a SO to be here.";
            break;
          case stReady:
            siInfo.status = stDelivered;
            siInfo.completedOnTrNum = tInfo.id;
            //siInfo.payment   = siInfo.price-siInfo.payment; //the final payment is what we save on db.
            myDb->updateSpecialOrder(siInfo);
            break;
          case stDelivered:
            qDebug()<<"There is an inappropiate state (Delivered) for a SO to be here.";
            break;
          default:
            qDebug()<<"No state for the SO, setting as InProgress";
            siInfo.status =stInProgress;
            myDb->updateSpecialOrder(siInfo);
            break;
        }
        //update special order info (when resume sale is used, deliveryDateTime is changed)
        myDb->updateSpecialOrder(siInfo);
        //update completingOrder flag
        completingOrder = siInfo.completePayment; //considering the last one at the end, all SO must have the same status!
      } //for each
    }// !specialOrders.isEmpty
    
    // taking into account the client discount. Applied over other products discount.
    // discMoney is the money discounted because of client discount.
    
    //double _taxes = 0;
    //if (!Settings::addTax()) _taxes = totalTax;
    // NOTE: If taxes included in price ( !addTax() ) the profit include tax amount.
    //       Taxes paid to the gov. are calculated with profit.TODO:VERIFY this information! accountants knowledge fro each country needed.
    utilidad = utilidad - discMoney; //The net profit  == profit minus the discount (client || occasional)
    if ( !specialOrders.isEmpty() ) {
      if ( !completingOrder ) {
        //This means there are special orders, and if they are NOT completing, so profit must be ZERO.
        utilidad = 0;
        qDebug()<<"STARTING A SPECIAL ORDER.   The Profit set to ZERO *****";
      }
    }
    tInfo.utility = utilidad; //NOTE: ? - _taxes;
    tInfo.itemlist  = productIDs.join(",");

    qDebug()<<"\nSALE NET PROFIT:"<< utilidad<<" discMoney:"<<discMoney<<"\n";

    //special orders Str on transactionInfo
    tInfo.specialOrders = ordersStr.join(","); //all special orders on the hash formated as id/qty,id/qty...

    //update transactions
    myDb->updateTransaction(tInfo);
    //increment client points
    myDb->incrementClientPoints(tInfo.clientid, tInfo.points);

    if (drawerCreated) {
        //FIXME: What to di first?... add or substract?... when there is No money or there is less money than the needed for the change.. what to do?
        if (ui_mainview.checkCash->isChecked()) {
          drawer->addCash(payWith);
          drawer->substractCash(changeGiven);
          drawer->incCashTransactions();
          //open drawer only if there is a printer available.
          if (Settings::openDrawer() && Settings::smallTicketDotMatrix() && Settings::printTicket())
            drawer->open();
        } else {
          drawer->incCardTransactions();
          drawer->addCard(payWith);
        }
        drawer->insertTransactionId(getCurrentTransaction());
    }
    else {
       //KMessageBox::error(this, i18n("The Drawer is not initialized, please start operation first."), i18n("Error") );
      KNotification *notify = new KNotification("information", this);
      notify->setText(i18n("The Drawer is not initialized, please start operation first."));
      QPixmap pixmap = DesktopIcon("dialog-information",32);
      notify->setPixmap(pixmap);
      notify->sendEvent();
    }

    //update client info in the hash....
    clientInfo.points += buyPoints;
    clientsHash.remove(QString::number(clientInfo.id));
    clientsHash.insert(QString::number(clientInfo.id), clientInfo);
    updateClientInfo();
    refreshTotalLabel();

    QString realSubtotal;
    if (Settings::addTax())
      //realSubtotal = KGlobal::locale()->formatMoney(subTotalSum-discMoney+soDiscounts+pDiscounts, QString(), 2);
      realSubtotal = KGlobal::locale()->formatMoney(subTotalSum+discMoney+soDiscounts+pDiscounts, QString(), 2);
    else
      realSubtotal = KGlobal::locale()->formatMoney(subTotalSum-totalTax+discMoney+soDiscounts+pDiscounts, QString(), 2); //FIXME: es +discMoney o -discMoney??
    qDebug()<<"\n >>>>>>>>> Real SUBTOTAL = "<<realSubtotal<<"  subTotalSum = "<<subTotalSum<<" ADDTAXES:"<<Settings::addTax()<<"  Disc:"<<discMoney;
    qDebug()<<"\n********** Total Taxes:"<<totalTax<<" total Discount:"<<discMoney<< " SO Discount:"<<soDiscounts<<" Prod Discounts:"<<pDiscounts;

    //Ticket
    ticket.number = currentTransaction;
    ticket.subTotal = realSubtotal; //This is the subtotal-taxes-discount
    ticket.total  = payTotal;
    ticket.change = changeGiven;
    ticket.paidwith = payWith;
    ticket.itemcount = cantidad;
    ticket.cardnum = cardNum;
    ticket.cardAuthNum = authnumber;
    ticket.paidWithCard = ui_mainview.checkCard->isChecked();
    ticket.clientDisc = clientInfo.discount;
    ticket.clientDiscMoney = discMoney;
    ticket.buyPoints = buyPoints;
    ticket.clientPoints = clientInfo.points;
    ticket.lines = ticketLines;
    ticket.clientid = clientInfo.id;
    ticket.hasSpecialOrders = !specialOrders.isEmpty();
    if (completePayments>0)
      ticket.completingSpecialOrder = true;
    else
      ticket.completingSpecialOrder = false;
    ticket.totalTax = totalTax;

    ticket.soTotal =  soGTotal;
    ticket.deliveryDT = soDeliveryDT;
    ticket.terminal = QString::number(tInfo.terminalnum);
    qDebug()<<" \n soGTotal:"<<soGTotal<<" deliveryDT:"<<soDeliveryDT<<"\n";

    if (printDTticket)
      printTicket(ticket);
    else {
        //if not printing ticket, it means it is config to not print date changed tickets.. but this affects to the freeze/unfreeze UI, and to call startAgain().
        freezeWidgets();
        QTimer::singleShot(500, this, SLOT(unfreezeWidgets()));
        qDebug()<<"Not printing ticket...";
      }

    //update balance
    qDebug()<<"FINISH TRANSACTION, UPDATING BALANCE #"<<currentBalanceId;
    updateBalance(false);//for sessions.
    
    transactionInProgress = false;
    updateModelView();
    ui_mainview.editItemCode->setFocus();

    //Check level of cash in drawer
    if (drawer->getAvailableInCash() < Settings::cashMinLevel() && Settings::displayWarningOnLowCash()) {
      //KPassivePopup::message( i18n("Warning:"),i18n("Cash level in drawer is low."),DesktopIcon("dialog-warning", 48), this);
      KNotification *notify = new KNotification("information", this);
      notify->setText(i18n("Cash level in drawer is low."));
      QPixmap pixmap = DesktopIcon("dialog-warning",32); //NOTE: This does not works
      notify->setPixmap(pixmap);
      notify->sendEvent();
    }
    
    delete myDb;
   }
   
   if (!ui_mainview.groupSaleDate->isHidden()) ui_mainview.groupSaleDate->hide(); //finally we hide the sale date group
   completingOrder = false; //cleaning flag
   oDiscountMoney = 0; //reset discount money... the new discount type.
}


void lemonView::printTicket(TicketInfo ticket)
{
  if (ticket.total == 0 && !Settings::printZeroTicket()) {
    freezeWidgets();
    KNotification *notify = new KNotification("information", this);
    notify->setText(i18n("The ticket was not printed because it is ZERO in the amount to pay. Just to save trees."));
    QPixmap pixmap = DesktopIcon("dialog-error",32);
    notify->setPixmap(pixmap);
    notify->sendEvent();
    QTimer::singleShot(2000, this, SLOT(unfreezeWidgets()));
    return; ///DO NOT PRINT! We are saving trees.
  }

    //TRanslateable strings:
  QString salesperson    = i18n("Salesperson: %1", loggedUserName);
  QString hQty           = i18n("Qty");
  QString hProduct       = i18n("Product");
  QString hPrice         = i18n("Price");
  QString hDisc          = i18n("Discount");
  QString hTotal         = i18n("Total");
  QString hClientDisc    = i18n("Your Personal Discount");
  QString hClientBuyPoints  = i18n("Your points this buy: %1", ticket.buyPoints); 
  QString hClientPoints  = i18n("Your total points: %1", ticket.clientPoints);
  QString hTicket  = i18n("# %1", ticket.number);
  QString terminal = i18n("Terminal #%1", ticket.terminal);
  QString hPrePayment = i18n("PRE PAYMENT OF");
  QString hCompletePayment = i18n("COMPLETED PAYMENT WITH");
  QString hNextPaymentStr = i18n("To complete your payment");
  QString hLastPaymentStr = i18n("Your pre payment");
  QString hSpecialOrder = i18n("SPECIAL ORDERS");
  QString hNotes = i18n("Notes:");
  QString hDeliveryDT = i18n("Delivery");
  QString hTax = i18n("Tax");
  QString hSubtotal = i18n("Subtotal");
  QString hTendered = i18n("Tendered");
  //HTML Ticket
  QStringList ticketHtml;
  double tDisc = 0.0;
  //Ticket header
  ticketHtml.append(QString("<html><body><b>%1 - %2</b> [%3]<br>Ticket #%4 %5 %6<br>")
      .arg(Settings::editStoreName())
      .arg(Settings::storeAddress())
      .arg(Settings::storePhone())
      .arg(ticket.number)
      .arg(salesperson)
      .arg(terminal));
  //Ticket Table header
  ticketHtml.append(QString("<table border=0><tr><th>%1</th><th>%2</th><th>%3</th><th>%4</th><th>%5</th></tr>")
      .arg(hQty).arg(hProduct).arg(hPrice).arg(hDisc).arg(hTotal));

  //TEXT Ticket
  QStringList itemsForPrint;
  QString line;
  line = QString("%1, %2").arg(Settings::editStoreName()).arg(Settings::storeAddress()); //FIXME:Check Address lenght for ticket
  itemsForPrint.append(line);
  line = QString("%1").arg(terminal);
  itemsForPrint.append(line);
  line = QString("%1  %2").arg(hTicket).arg(salesperson);
  itemsForPrint.append(line);
  line = KGlobal::locale()->formatDateTime(ticket.datetime, KLocale::LongDate);
  itemsForPrint.append(line);
  itemsForPrint.append("          ");
  hQty.append("      ").truncate(6);
  hProduct.append("              ").truncate(14);
  hPrice.append("       ").truncate(7);
  hTotal.append("      ").truncate(6);
  //qDebug()<< "Strings:"<< hQty;qDebug()<< ", "<< hProduct<<", "<<hPrice<<", "<<hTotal;
  itemsForPrint.append(hQty +"  "+ hProduct +"  "+ hPrice+ "  "+ hTotal);
  itemsForPrint.append("------  --------------  -------  -------");

  QLocale localeForPrinting; // needed to convert double to a string better 
  for (int i = 0; i < ticket.lines.size(); ++i)
  {
    TicketLineInfo tLine = ticket.lines.at(i);
    QString  idesc =  tLine.desc;
    QString iprice =  localeForPrinting.toString(tLine.price,'f',2);
    QString iqty   =  localeForPrinting.toString(tLine.qty,'f',2);
    if (iprice.endsWith(".00") || iprice.endsWith(",00")) { iprice.chop(3); }//we chop the trailing zeroes...
    if (iqty.endsWith(".00") || iqty.endsWith(",00")) { iqty.chop(3); }//we chop the trailing zeroes...
    iqty = iqty+" "+tLine.unitStr;
    QString idiscount =  localeForPrinting.toString(/*tLine.qty**/tLine.disc,'f',2);
    bool hasDiscount = false;
    if (tLine.disc > 0) {
      hasDiscount = true;
      tDisc = tDisc + /*tLine.qty**/tLine.disc;
    }
    QString idue =  localeForPrinting.toString(tLine.total,'f',2);
    if (idue.endsWith(".00") || idue.endsWith(",00")) { idue.chop(3); }//we chop the trailing zeroes...

    //get contents, remove the first which is the name of the SO
    QStringList contentsList = tLine.geForPrint.split("|");
    if (!contentsList.isEmpty()) contentsList.removeFirst();
    
    //HTML Ticket
    ticketHtml.append(QString("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
        .arg(iqty).arg(idesc).arg(iprice).arg(idiscount).arg(idue));
    if (!tLine.geForPrint.isEmpty()) {
      ticketHtml.append(QString("<tr><td></td><td>%1</td><td></td><td></td><td></td></tr>").arg(contentsList.join("\n")));
      if (tLine.completePayment) {
        if (!tLine.isGroup)
          ticketHtml.append(QString("<tr><td></td><td>%1</td><td></td><td></td><td></td></tr>").arg(hCompletePayment+QString::number(tLine.payment*tLine.qty)));
      }
      else {
        if (!tLine.isGroup)
          ticketHtml.append(QString("<tr><td></td><td>%1</td><td></td><td></td><td></td></tr>").arg(hPrePayment+QString::number(tLine.payment*tLine.qty)));
      }
    }
    //TEXT TICKET
    //adjusting length
    if (idesc.length()>14) idesc.truncate(14); //idesc = idesc.insert(19, '\n');
    else {
      while (idesc.length()<14) idesc = idesc.insert(idesc.length(), ' ');
    }
    iqty.append("      ").truncate(6);
    while (iprice.length()<7) iprice = QString(" ").append(iprice);
    while (idue.length()<7) idue = QString(" ").append(idue);
    
//     while (iqty.length()<7) iqty = iqty.insert(iqty.length(), ' ');
//     while (idiscount.length()<4) idiscount = idiscount.insert(idiscount.length(), ' ');

    line = QString("%1  %2  %3  %4").
        arg(iqty).
        arg(idesc).
        arg(iprice).
        arg(idue);
    itemsForPrint.append(line);
    if (!contentsList.isEmpty()) {
      itemsForPrint<<contentsList;
      if (tLine.completePayment) {
        if (!tLine.isGroup)
          itemsForPrint<<(hCompletePayment+" "+QString::number(tLine.payment/**tLine.qty*/));
      }
      else {
        if (!tLine.isGroup)
          itemsForPrint<<(hPrePayment+" "+QString::number(tLine.payment/**tLine.qty*/));
      }
      if (!tLine.isGroup && tLine.payment > 0) {
        //print the delivery date.
        itemsForPrint<<(hDeliveryDT+" "+KGlobal::locale()->formatDateTime(tLine.deliveryDateTime,  KLocale::ShortDate));
      }
    }
    if (hasDiscount) itemsForPrint.append(QString("        * %1 *     -%2").arg(hDisc).arg(idiscount) );
  }//for each item

  tDisc = tDisc + ticket.clientDiscMoney;

  //HTML Ticket
  QString harticles = i18np("%1 article.", "%1 articles.", ticket.itemcount);
  QString htotal    = i18n("A total of");
  ticketHtml.append(QString("</table><br><br><b>%1</b> %2 <b>%3</b>")
      .arg(harticles).arg(htotal).arg(KGlobal::locale()->formatMoney(ticket.total, QString(), 2)));
  ticketHtml.append(i18n("<br>Paid with %1, your change is <b>%2</b><br>",
                          KGlobal::locale()->formatMoney(ticket.paidwith, QString(), 2),
                          KGlobal::locale()->formatMoney(ticket.change, QString(), 2)));
  ticketHtml.append(Settings::editTicketMessage());
  //Text Ticket
  itemsForPrint.append("  ");
  line = QString("%1  %2 %3").arg(harticles).arg(htotal).arg(KGlobal::locale()->formatMoney(ticket.total, QString(), 2));
  itemsForPrint.append(line);
  if (tDisc > 0) {
    line = i18n("You saved %1", KGlobal::locale()->formatMoney(tDisc, QString(), 2));
    itemsForPrint.append(line);
  }
  if (ticket.clientDiscMoney>0) itemsForPrint.append(hClientDisc+": "+QString::number(ticket.clientDiscMoney));
  if (ticket.buyPoints>0 && ticket.clientid>1) itemsForPrint.append(hClientBuyPoints);
  if (ticket.clientPoints>0 && ticket.clientid>1) itemsForPrint.append(hClientPoints);
  itemsForPrint.append(" ");
  line = i18n("Paid with %1, your change is %2",
              KGlobal::locale()->formatMoney(ticket.paidwith, QString(), 2), KGlobal::locale()->formatMoney(ticket.change, QString(), 2));
  itemsForPrint.append(line);
  itemsForPrint.append(" ");
  if (ticket.paidWithCard) {
    ticketHtml.append(i18n("<br>Card # %1<br>Authorisation # %2",ticket.cardnum, ticket.cardAuthNum));
    line = i18n("Card Number:%1 \nAuthorisation #:%2",ticket.cardnum,ticket.cardAuthNum);
    itemsForPrint.append(line);
    itemsForPrint.append(" ");
  }
  line = QString(Settings::editTicketMessage());
  itemsForPrint.append(line);
  ticketHtml.append("</body></html>");

  //Printing...
  qDebug()<< itemsForPrint.join("\n");

  //tDisc = tDisc + ticket.clientDiscMoney; NOTE: moved above, aug 7 2011. text ticket did not get clientDiscMoney.

  ///Real printing... [sendind data to print-methods]
  if (Settings::printTicket()) {
    if (Settings::smallTicketDotMatrix()) {
      QString printerFile=Settings::printerDevice();
      if (printerFile.length() == 0) printerFile="/dev/lp0";
      QString printerCodec=Settings::printerCodec();
      PrintDEV::printSmallTicket(printerFile, printerCodec, itemsForPrint.join("\n"));
    } //smalTicket
    else if (Settings::smallTicketCUPS() ) { // some code inspired on Daniel O'Neill code.
      qDebug()<<"Printing small receipt using CUPS";
      PrintTicketInfo ptInfo;
      //NOTE: Apr 14 2011: If we print to a dot-matrix printer using CUPS, then we need to REMOVE PIXMAPS because they will print pages of strange characters.
      //FIXME: We need an extra option in config for this (using cups for dot-matrix printers)
      QPixmap logoPixmap;
      logoPixmap.load(Settings::storeLogo());

      //foreach(TicketLineInfo li, ticket.lines) {
      //  qDebug()<<"TicketLine.geForPrint:"<<li.geForPrint;
      //}


      Azahar *myDb = new Azahar;
      myDb->setDatabase(db);
      QString clientName = myDb->getClientInfo(ticket.clientid).name;

      ptInfo.ticketInfo = ticket;
      ptInfo.storeLogo  = logoPixmap;
      ptInfo.storeName  = Settings::editStoreName();
      ptInfo.storeAddr  = Settings::storeAddress();
      ptInfo.storePhone = Settings::storePhone();
      ptInfo.ticketMsg  = Settings::editTicketMessage();
      ptInfo.salesPerson= loggedUserName;
      ptInfo.terminal   = terminal;
      ptInfo.thPhone    = i18n("Phone: ");
      ptInfo.thDate     = KGlobal::locale()->formatDateTime(ptInfo.ticketInfo.datetime, KLocale::LongDate);
      ptInfo.thTicket   = hTicket;
      ptInfo.thProduct  = hProduct;
      ptInfo.thQty      = i18n("Qty");
      ptInfo.thPrice    = hPrice;
      ptInfo.thDiscount = hDisc;
      ptInfo.thTotal    = hTotal;
      ptInfo.thTotals   = KGlobal::locale()->formatMoney(ptInfo.ticketInfo.total, QString(), 2);
      ptInfo.thPoints   = i18n(" %3 [ %4 ]| You got %1 points | Your accumulated is :%2 | ", ticket.buyPoints, ticket.clientPoints, clientName, ticket.clientid);
      ptInfo.thArticles = i18np("%1 article.", "%1 articles.", ptInfo.ticketInfo.itemcount);
      ptInfo.thPaid     = KGlobal::locale()->formatMoney(ptInfo.ticketInfo.paidwith, QString(), 2);
      ptInfo.thChange   = KGlobal::locale()->formatMoney(ptInfo.ticketInfo.change, QString(), 2);
      ptInfo.thChangeStr= i18n("Change");
      ptInfo.tDisc      = KGlobal::locale()->formatMoney(-tDisc, QString(), 2);
      ptInfo.thCard     = i18n("Card Number  : %1", ticket.cardnum);
      ptInfo.thCardAuth = i18n("Authorization : %1", ticket.cardAuthNum);
      ptInfo.totDisc    = tDisc;
      ptInfo.subtotal   = ticket.subTotal;
      ptInfo.logoOnTop = Settings::chLogoOnTop();
      //QString signM = KGlobal::locale()->formatMoney(tDisc, QString(), 2);
      //signM.truncate(2); //NOTE: this is only getting the sign "$"...
      ptInfo.paymentStrPrePayment = hPrePayment;
      ptInfo.paymentStrComplete = hCompletePayment;
      ptInfo.nextPaymentStr = hNextPaymentStr;
      ptInfo.lastPaymentStr = hLastPaymentStr;
      ptInfo.deliveryDateStr= hDeliveryDT;
      ptInfo.clientDiscMoney = ticket.clientDiscMoney;
      ptInfo.clientDiscountStr = hClientDisc;
      ptInfo.randomMsg = myDb->getRandomMessage(rmExcluded, rmSeason);
      ptInfo.taxes = KGlobal::locale()->formatMoney(ticket.totalTax, QString(), 2);
      ptInfo.thTax = hTax;
      ptInfo.thSubtotal = hSubtotal;
      ptInfo.thTendered = hTendered;
      //for reservations
      ptInfo.hdrReservation = i18n(" RESERVATION ");
      if (!ptInfo.ticketInfo.reservationStarted)
        ptInfo.resTotalAmountStr  = i18n("Purchase Total Amount");
      else
        ptInfo.resTotalAmountStr  = i18n("Reservation Total Amount");

      QPrinter printer;
      printer.setFullPage( true );
      
      //This is lost when the user chooses a printer. Because the printer overrides the paper sizes.
      printer.setPageMargins(0,0,0,0,QPrinter::Millimeter);
      printer.setPaperSize(QSizeF(22,200), QPrinter::Millimeter); //setting small ticket paper size. 72mm x 200mm

      QString pName = printer.printerName(); //default printer name
      
      QPrintDialog printDialog( &printer );
      printDialog.setWindowTitle(i18n("Print Receipt -- Press ESC to export to PDF to your home/lemon-printing/ folder"));

      if ( printDialog.exec() ) {
        //this overrides what the user chooses if he does change sizes and margins.
        printer.setPageMargins(0,0,0,0,QPrinter::Millimeter);
        printer.setPaperSize(QSizeF(72,200), QPrinter::Millimeter); //setting small ticket paper size. 72mm x 200mm
        //TODO: Set Copies: printer.setCopyCount(2); //NOTE:Introduced in Qt 4.7 --WARNING-- See also setCollateCopies()
        PrintCUPS::printSmallTicket(ptInfo, printer);
      } else {
          //NOTE: This is a proposition:
          //      If the dialog is accepted (ok), then we print what the user choosed. Else, we print to a file (PDF).
          //      The user can press ENTER when dialog appearing if the desired printer is the default (or the only).
          qDebug()<<"User cancelled printer dialog. We export ticket to a file.";
          QString fn = QString("%1/lemon-printing/").arg(QDir::homePath());
          QDir dir;
          if (!dir.exists(fn))
              dir.mkdir(fn);
          fn = fn+QString("ticket-%1__%2.pdf").arg(ticket.number).arg(ticket.datetime.date().toString("dd-MMM-yy"));
          qDebug()<<fn;
          
          printer.setOutputFileName(fn);
          printer.setPageMargins(0,0,0,0,QPrinter::Millimeter);
          printer.setPaperSize(QSizeF(72,200), QPrinter::Millimeter); //setting small ticket paper size. 72mm x 200mm

          PrintCUPS::printSmallTicket(ptInfo, printer);
      }
      delete myDb;
    }
    else {
      qDebug()<<"Printing big receipt using CUPS";

      PrintTicketInfo ptInfo;
      QPixmap logoPixmap;
      logoPixmap.load(Settings::storeLogo());

      Azahar *myDb = new Azahar;
      myDb->setDatabase(db);
      QString clientName = myDb->getClientInfo(ticket.clientid).name;
      
      ptInfo.ticketInfo = ticket;
      ptInfo.storeLogo  = logoPixmap;
      ptInfo.storeName  = Settings::editStoreName();
      ptInfo.storeAddr  = Settings::storeAddress();
      ptInfo.storePhone = Settings::storePhone();
      ptInfo.ticketMsg  = Settings::editTicketMessage();
      ptInfo.salesPerson= loggedUserName;
      ptInfo.terminal   = terminal;
      ptInfo.thPhone    = i18n("Phone: ");
      ptInfo.thDate     = KGlobal::locale()->formatDateTime(ptInfo.ticketInfo.datetime, KLocale::LongDate);
      ptInfo.thTicket   = hTicket;
      ptInfo.thProduct  = hProduct;
      ptInfo.thQty      = i18n("Qty");
      ptInfo.thPrice    = hPrice;
      ptInfo.thDiscount = hDisc;
      ptInfo.thTotal    = hTotal;
      ptInfo.thTotals   = KGlobal::locale()->formatMoney(ptInfo.ticketInfo.total, QString(), 2);
      ptInfo.thPoints   = i18n(" %3 [ %4 ]| You got %1 points | Your accumulated is :%2 | ", ticket.buyPoints, ticket.clientPoints, clientName, ticket.clientid);
      ptInfo.thArticles = i18np("%1 article.", "%1 articles.", ptInfo.ticketInfo.itemcount);
      ptInfo.thPaid     = ""; //i18n("Paid with %1, your change is %2", KGlobal::locale()->formatMoney(ptInfo.ticketInfo.paidwith, QString(), 2),KGlobal::locale()->formatMoney(ptInfo.ticketInfo.change, QString(), 2) );
      ptInfo.tDisc      = KGlobal::locale()->formatMoney(-tDisc, QString(), 2);
      ptInfo.subtotal   = ticket.subTotal;
      ptInfo.totDisc    = tDisc;
      ptInfo.logoOnTop = Settings::chLogoOnTop();
      ptInfo.clientDiscMoney = ticket.clientDiscMoney;
      ptInfo.clientDiscountStr = hClientDisc;
      ptInfo.randomMsg = myDb->getRandomMessage(rmExcluded, rmSeason);

      QPrinter printer;
      printer.setFullPage( true );
      QPrintDialog printDialog( &printer );
      printDialog.setWindowTitle(i18n("Print Receipt"));
      if ( printDialog.exec() ) {
        PrintCUPS::printBigTicket(ptInfo, printer);
      }
      delete myDb;
    }//bigTicket
    //now if so.. print it
    if (Settings::printSO() && ticket.hasSpecialOrders && !ticket.completingSpecialOrder){
      qDebug()<<"Printing small receipt for SPECIAL ORDERS using CUPS";
      PrintTicketInfo ptInfo;
      
      ptInfo.ticketInfo = ticket;
      ptInfo.storeName  = hSpecialOrder; //user for header
      ptInfo.salesPerson= loggedUserName;
      ptInfo.terminal   = terminal;
      ptInfo.thDate     = KGlobal::locale()->formatDateTime(ptInfo.ticketInfo.datetime, KLocale::LongDate);
      ptInfo.thTicket   = hTicket;
      ptInfo.thProduct  = hProduct;
      ptInfo.thQty      = i18n("Qty");
      ptInfo.thPrice    = hPrice;
      ptInfo.thTotal    = hTotal;
      ptInfo.thTotals   = KGlobal::locale()->formatMoney(ptInfo.ticketInfo.total, QString(), 2);
      QString signM = KGlobal::locale()->formatMoney(tDisc, QString(), 2);
      signM.truncate(2); //this gets the $ only...
      ptInfo.paymentStrPrePayment = hPrePayment + signM;
      ptInfo.paymentStrComplete = hCompletePayment + signM;
      ptInfo.nextPaymentStr = hNextPaymentStr;
      ptInfo.lastPaymentStr = hLastPaymentStr;
      ptInfo.deliveryDateStr = hDeliveryDT;
      
      
      QPrinter printer;
      printer.setFullPage( true );
      QPrintDialog printDialog( &printer );
      printDialog.setWindowTitle(i18n("Print Special Order Instructions "));
      if ( printDialog.exec() ) {
        PrintCUPS::printSmallSOTicket(ptInfo, printer);
      }
    }//soticket
  } //printTicket

  freezeWidgets();

  if (Settings::showDialogOnPrinting())
  {
    TicketPopup *popup = new TicketPopup(this, ticketHtml.join(" "), DesktopIcon("lemon-printer", 48), Settings::ticketTime()*1000);
    connect (popup, SIGNAL(onTicketPopupClose()), this, SLOT(unfreezeWidgets()) );
    QApplication::beep();
    popup->popup();
  } else {
    QTimer::singleShot(Settings::ticketTime()*1000, this, SLOT(unfreezeWidgets())); //ticket time used to allow the cashier to see the change to give the user... is configurable.
  }
}

void lemonView::freezeWidgets()
{
  emit signalDisableUI();
}

void lemonView::unfreezeWidgets()
{
  emit signalEnableUI();
  startAgain();
}

void lemonView::startAgain()
{
  qDebug()<<"startAgain(): New Transaction";
  productsHash.clear();
  specialOrders.clear();
  setupClients(); //clear the clientInfo (sets the default client info)
  clearUsedWidgets();
  buyPoints =0;
  discMoney=0;
  refreshTotalLabel();
  createNewTransaction(tSell);
  reservationPayment = 0; //reservationPayment is for reservations only!
  availabilityDoesNotMatters = false; // Only for reservations for now!
  doNotAddMoreItems = false;
  finishingReservation = false;
  startingReservation = false;
  reservationId = 0;
  //qDebug()<<" doNotAddMoreItems = false. FinishingReservation = false.";
  refreshTotalLabel();
  //NOTE: when createNewTRansaction is executed, and we are exiting application, the requested transaction create in not completed.. due to the way it is implemented in qtsql. This is a good thing for me because i dont want to create a new transaction when we are exiting lemon. Thats why the message "QSqlDatabasePrivate::removeDatabase: connection 'qt_sql_default_connection' is still in use, all queries will cease to work." appears on log (std output) when exiting lemon. This method is called from corteDeCaja which is called from cancelByExit() method which is called by lemon class on exit-query.
  //NOTE: But if we want to save something when exiting lemon -sending the query at the end- we must check for it to complete. In this case, this is the last query sent... but later with more code aded, could be a danger not to check for this things.
}

void lemonView::cancelCurrentTransaction()
{
  bool continueIt = false;
  if ( !Settings::lowSecurityMode() ) {
    if (Settings::requiereDelAuth() ) {
      dlgPassword->show();
      dlgPassword->hide();
      dlgPassword->clearLines();
      if ( dlgPassword->exec() ) continueIt=true;
    } else continueIt=true; //if requiereDelAuth
  } else continueIt=true; //if no low security

  if (continueIt) cancelTransaction(getCurrentTransaction());
}


void lemonView::preCancelCurrentTransaction()
{
  if (ui_mainview.tableWidget->rowCount()==0 ) { //empty transaction
    productsHash.clear();
    specialOrders.clear();
    setupClients(); //clear the clientInfo (sets the default client info)
    clearUsedWidgets();
    buyPoints =0;
    discMoney=0;
    refreshTotalLabel();
    qDebug()<<"** Cancelling an empty transaction [updating transaction]";
    updateTransaction();
    ///Next two lines were deleted to do not increment transactions number. reuse it.
    //if (Settings::deleteEmptyCancelledTransactions()) deleteCurrentTransaction();
    //else cancelCurrentTransaction();
  }
  else {
    cancelCurrentTransaction();
    //updateTransaction();
    //updateBalance(false);
  }

  //if change sale date is in progress (but cancelled), hide it.
  if (!ui_mainview.groupSaleDate->isHidden()) {
    ui_mainview.groupSaleDate->hide();
  }
}

void lemonView::deleteCurrentTransaction()
{
  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  if (myDb->deleteTransaction(getCurrentTransaction())) {
    transactionInProgress=false;
    createNewTransaction(tSell);
  }
  else {
    KMessageBox::detailedError(this, i18n("Lemon has encountered an error when querying the database, click details to see the error details."), myDb->lastError(), i18n("Delete Transaction: Error"));
  }
  delete myDb;
}

//NOTE: This substracts points given to the client, restore stockqty, register a cashout for the money return. All if it applies.
void lemonView::cancelTransaction(qulonglong transactionNumber)
{
  bool enoughCashAvailable=false;
  bool transToCancelIsInProgress=false;
  bool transCompleted=false;
  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  //get amount to return
  TransactionInfo tinfo = myDb->getTransactionInfo(transactionNumber);
  if (tinfo.state == tCancelled && tinfo.id >0) transCompleted = true;

  if (getCurrentTransaction() == transactionNumber) {
    ///this transaction is not saved yet (more than the initial data when transaction is created)
    //UPDATE: Now each time a product is inserted or screen locked, transaction and balance is saved.
    if (finishingReservation) {
        postponeReservation();
        startAgain();
//         productsHash.clear();
//         specialOrders.clear();
//         setupClients(); //clear the clientInfo (sets the default client info)
//         clearUsedWidgets();
//         buyPoints =0;
//         discMoney=0;
//         finishingReservation = false;
//         startingReservation  = false;
//         transactionInProgress= false;
//         doNotAddMoreItems    = false;
//         createNewTransaction(tSell);
//         refreshTotalLabel();
        return;
    } else {
        transToCancelIsInProgress = true;
        clearUsedWidgets();
        refreshTotalLabel();
    }
  } else {
    ///this transaction is saved (amount,products,points...)
    clearUsedWidgets();
    refreshTotalLabel();
    if (drawer->getAvailableInCash() > tinfo.amount && tinfo.id>0){ // == or >= or > ?? to dont let empty the drawer
      enoughCashAvailable = true;
    }
  }
  
  //Mark as cancelled if possible
  
  //Check if payment was with cash.
  //FIXME: Allow card payments to be cancelled!!! DIC 2009
  if (tinfo.paymethod != 1 ) {
    KNotification *notify = new KNotification("information", this);
    notify->setText(i18n("The ticket cannot be cancelled because it was paid with a credit/debit card."));
    QPixmap pixmap = DesktopIcon("dialog-error",32);
    notify->setPixmap(pixmap);
    notify->sendEvent();
    return;
  }
  
  if  (enoughCashAvailable || transToCancelIsInProgress) {
    qDebug()<<" ok, trans is in progress or cash is enough";
    if (myDb->cancelTransaction(transactionNumber, transToCancelIsInProgress)) {
      QString authBy = dlgPassword->username();
      if (authBy.isEmpty()) authBy = myDb->getUserName(1); //default admin.
      log(loggedUserId, QDate::currentDate(), QTime::currentTime(), i18n("Cancelling transaction #%1. Authorized by %2",transactionNumber,authBy));
      qDebug()<<"Cancelling ticket was ok";
      if (transCompleted) {
        //if was completed, then return the money...
        if (tinfo.paymethod == 1 ) {//1 is cash
          drawer->substractCash(tinfo.amount);
          if (Settings::openDrawer() && Settings::smallTicketDotMatrix() && Settings::printTicket()) drawer->open();
        }
        //Inform to the user.
        KNotification *notify = new KNotification("information", this);
        notify->setText(i18n("The ticket was sucessfully cancelled."));
        QPixmap pixmap = DesktopIcon("dialog-error",32);
        notify->setPixmap(pixmap);
        notify->sendEvent();
      }
      transactionInProgress = false; //reset
      createNewTransaction(tSell);
    }
    else { //myDB->cancelTransaction() returned some error...
      qDebug()<<"Not cancelled!";
      if (!transToCancelIsInProgress) {
        KNotification *notify = new KNotification("information", this);
        notify->setText(i18n("Error cancelling ticket: %1",myDb->lastError()));
        QPixmap pixmap = DesktopIcon("dialog-error",32);
        notify->setPixmap(pixmap);
        notify->sendEvent();
      } else {
        //Reuse the transaction instead of creating a new one.
        qDebug()<<"Transaction to cancel is in progress. Clearing all to reuse transaction number...";
        productsHash.clear();
        specialOrders.clear();
        setupClients(); //clear the clientInfo (sets the default client info)
        clearUsedWidgets();
        buyPoints =0;
        discMoney=0;
        refreshTotalLabel();
        qDebug()<<"** Cancelling current transaction [updating transaction]";
        updateTransaction();
      }
    }
  } else {
    //not cash available in drawer to return money to the client OR transaction id does not exists
    QString msg;
    if (tinfo.id > 0)
      msg = i18n("There is not enough cash available in the drawer.");
    else
      msg = i18n("Ticket to cancel does not exists!");

    KNotification *notify = new KNotification("information", this);
    notify->setText(msg);
    QPixmap pixmap = DesktopIcon("dialog-error",32);
    notify->setPixmap(pixmap);
    notify->sendEvent();
  }
  delete myDb;
}


void lemonView::startOperation(const QString &adminUser)
{
  qDebug()<<"Starting operations...";
  operationStarted = false;
  bool ok=false;
  double qty=0.0; //TODO:preset as the money on the drawer on the last user.
  InputDialog *dlg = new InputDialog(this, false, dialogMoney, i18n("Enter the amount of money to deposit in the drawer"));
  dlg->setEnabled(true);
  if (dlg->exec() ) {
    qty = dlg->dValue;
    if (qty >= 0) ok = true; //allow no deposit...
  }

  if (ok) {
    if (!drawerCreated) {
      drawer = new Gaveta();
      drawer->setPrinterDevice(Settings::printerDevice());
      drawerCreated = true;
    }
    //NOTE: What about CUPS printers? Some of them can be configured to open drawer when printing.
     if (Settings::openDrawer() && Settings::smallTicketDotMatrix() && Settings::printTicket()) drawer->open();
   // Set drawer amount.
    drawer->reset();
    drawer->setStartDateTime(QDateTime::currentDateTime());
    drawer->setAvailableInCash(qty); //this also sets the available in card amount
    drawer->setInitialAmount(qty);
    operationStarted = true;
    createNewTransaction(tSell);
    emit signalStartedOperation();
    log(loggedUserId, QDate::currentDate(), QTime::currentTime(), i18n("Operation Started by %1 at terminal %2",
    adminUser, Settings::editTerminalNumber()));
  }
  else {
    qDebug()<<"Starting Operations cancelled...";
    emit signalEnableStartOperationAction();
    emit signalNoLoggedUser();
  }

  //SESSIONS DEC 28 2009
  if (currentBalanceId <= 0 ) {
    qDebug()<<"StartOperations::INSERT_BALANCE";
    insertBalance(); //this updates the currentBalanceId
  }
  else {
    qDebug()<<"StartOperations::UPDATE_BALANCE [should not occurr, balanceId="<<currentBalanceId<<"]";
    updateBalance(false);
  }
  
  ui_mainview.editItemCode->setFocus();
}

//this method is for lemon.cpp's action connect for button, since button's trigger(bool) will cause to ask = trigger's var.
void lemonView::_slotDoStartOperation()
{
  //simply call the other...
  slotDoStartOperation(true);
}

void lemonView::slotDoStartOperation(const bool &ask)
{
  //NOTE: For security reasons, we must ask for admin's password.
  //But, can we be more flexible -for one person store- and do not ask for password in low security mode
  // is ask is true we ask for auth, else we dont because it was previously asked for (calling method from corteDeCaja)

  //qDebug()<<"bool ask = "<<ask;
  
  qDebug()<<"doStartOperations..";
  if (!operationStarted) {
    bool doit = false;
    if (Settings::lowSecurityMode() || !ask) {
      doit = true;
    } else {
        do  {
          dlgPassword->show();
          dlgPassword->clearLines();
          dlgPassword->hide();
          doit = dlgPassword->exec();
        } while (!doit);
    }//else lowsecurity
    QString adminU;
    if (dlgPassword->username().isEmpty()) {
      Azahar *myDb = new Azahar;
      myDb->setDatabase(db);
      adminU = myDb->getUserName(1);//default admin.
      delete myDb;
    }
    if (doit) startOperation(adminU);
  }
}

/* REPORTS ZONE */
/*--------------*/

void lemonView::doCorteDeCaja()
{
  qDebug()<<"logged user:"<<loggedUser;
  //This is called only from the UI (via Button or shortcut) -- request by the user.
  //We force a login, login forces a corteDeCaja if needed.
  login();
}

void lemonView::corteDeCaja()
{
  //Balance: Where there are no transactions, we dont need to doit.
  //         Also consider the cash in the drawer.
  //         Also, when doing a Balance, we need to force login.
  qDebug()<<"Transactions Count:"<<drawer->getTransactionsCount()<<" Cash in drawer:"<<drawer->getAvailableInCash();
  bool yes=false;
  if (drawer->getTransactionsCount()>0 || drawer->getAvailableInCash()>0) yes=true;
  if (!yes) {
    //     KNotification *notify = new KNotification("information", this);
    //     notify->setText(i18n("There are no transactions to inform or cash in the drawer."));
    //     QPixmap pixmap = DesktopIcon("dialog-information",32);
    //     notify->setPixmap(pixmap);
    //     if (!loggedUser.isEmpty())
    //       notify->sendEvent();
    
    //Things to do even if balance is not needed.
    operationStarted = false;
    currentBalanceId = 0;
    startAgain();
    return;
  }
  
  bool doit = false;
  //ASK for security if no lowSecurityMode.
  if (Settings::lowSecurityMode()) {
    doit = true;
  } else {
    dlgPassword->show();
    dlgPassword->clearLines();
    dlgPassword->hide();
    doit = dlgPassword->exec();
  }//else lowsecurity

  if (doit) {
    qDebug()<<"Doing Balance..";
    preCancelCurrentTransaction();
    QStringList lines;
    QStringList linesHTML;
    QString line;

    QString dId;
    QString dAmount;
    QString dHour;
    QString dMinute;
    QString dPaidWith;

    PrintBalanceInfo pbInfo;

    updateBalance(true); //now it is finished.
    pbInfo.thBalanceId = i18n("Balance Id:%1", currentBalanceId);
    //NOTE: saveBalance was replaced by updateBalance(currentBalanceId)

    // Create lines to print and/or show on dialog...

    //----------Translated strings--------------------
    QString strTitle      = i18n("%1 at Terminal # %2", loggedUserName, Settings::editTerminalNumber());
    QString strInitAmount = i18n("Initial Amount deposited:");
    QString strInitAmountH= i18n("Deposit");
    QString strInH         = i18n("In");
    QString strOutH        = i18n("Out");
    QString strInDrawerH   = i18n("In Drawer");
    QString strTitlePre    = i18n("Drawer Balance");
    QString strTitleTrans  = i18n("Transactions Details");
    QString strTitleTransH = i18n("Transactions");
    QString strId          = i18n("Id");
    QString strTimeH       = i18n("Time");
    QString strAmount      = i18n("Amount");
    QString strPaidWith    = i18n("Paid");
    QString strPayMethodH  = i18n("Method");
    pbInfo.reservationNote = i18n("When Completing Reservations, the Amount may be grater than Paid (amount) because of the reservation payment.");

    QPixmap logoPixmap;
    logoPixmap.load(Settings::storeLogo());

    pbInfo.storeName = Settings::editStoreName();
    pbInfo.storeAddr = Settings::storeAddress();
    pbInfo.storeLogo = logoPixmap;
    pbInfo.thTitle     = strTitle;
    pbInfo.thDeposit   = strInitAmountH;
    pbInfo.thIn        = strInH;
    pbInfo.thOut       = strOutH;
    pbInfo.thInDrawer  = strInDrawerH;
    pbInfo.thTitleDetails = strTitleTrans;
    pbInfo.thTrId      = strId;
    pbInfo.thTrTime    = strTimeH;
    pbInfo.thTrAmount  = strAmount;
    pbInfo.thTrPaidW    = strPaidWith;
    pbInfo.thTrPayMethod=strPayMethodH;
    pbInfo.startDate   = i18n("Start: %1",KGlobal::locale()->formatDateTime(drawer->getStartDateTime(), KLocale::LongDate));
    pbInfo.endDate     = i18n("End  : %1",KGlobal::locale()->formatDateTime(QDateTime::currentDateTime(), KLocale::LongDate));
    //Qty's
    pbInfo.initAmount = KGlobal::locale()->formatMoney(drawer->getInitialAmount(), QString(), 2);
    pbInfo.inAmount   = KGlobal::locale()->formatMoney(drawer->getInAmount(), QString(), 2);
    pbInfo.outAmount  = KGlobal::locale()->formatMoney(drawer->getOutAmount(), QString(), 2);
    pbInfo.cashAvailable=KGlobal::locale()->formatMoney(drawer->getAvailableInCash(), QString(), 2);
    pbInfo.logoOnTop = Settings::chLogoOnTop();
    pbInfo.thTitleCFDetails = i18n("Cash flow Details");
    pbInfo.thCFType    = i18n("Type");
    pbInfo.thCFReason  = i18n("Reason");
    pbInfo.thCFDate    = i18n("Time");
    pbInfo.notCashNote = i18n("This payment was not paid in cash.");


  //TODO: Hacer el dialogo de balance mejor, con un look uniforme a los demas dialogos.
  //       Incluso insertar imagenes en el html del dialogo.

    //HTML
    line = QString("<html><body><h3>%1</h3>").arg(strTitle);
    linesHTML.append(line);
    line = QString("<center><table border=1 cellpadding=5><tr><th colspan=4>%9</th></tr><tr><th>%1</th><th>%2</th><th>%3</th><th>%4</th></tr><tr><td>%5</td><td>%6</td><td>%7</td><td>%8</td></tr></table></ceter><br>")
        .arg(strInitAmountH)
        .arg(strInH)
        .arg(strOutH)
        .arg(strInDrawerH)
        .arg(KGlobal::locale()->formatMoney(drawer->getInitialAmount(), QString(), 2))
        .arg(KGlobal::locale()->formatMoney(drawer->getInAmount(), QString(), 2))
        .arg(KGlobal::locale()->formatMoney(drawer->getOutAmount(), QString(), 2))
        .arg(KGlobal::locale()->formatMoney(drawer->getAvailableInCash(), QString(), 2))
        .arg(strTitlePre);
    linesHTML.append(line);
    line = QString("<table border=1 cellpadding=5><tr><th colspan=5>%1</th></tr><tr><th>%2</th><th>%3</th><th>%4</th><th>%5</th><th>%6</th></tr>")
        .arg(strTitleTransH)
        .arg(strId)
        .arg(strTimeH)
        .arg(strAmount)
        .arg(strPaidWith)
        .arg(strPayMethodH);
    linesHTML.append(line);

    //TXT
    lines.append(strTitle);
    line = QString(KGlobal::locale()->formatDateTime(QDateTime::currentDateTime(), KLocale::LongDate));
    lines.append(line);
    lines.append("----------------------------------------");
    line = QString("%1 %2").arg(strInitAmount).arg(KGlobal::locale()->formatMoney(drawer->getInitialAmount(), QString(), 2));
    lines.append(line);
    line = QString("%1 :%2, %3 :%4")
        .arg(strInH)
        .arg(KGlobal::locale()->formatMoney(drawer->getInAmount(), QString(), 2))
        .arg(strOutH)
        .arg(KGlobal::locale()->formatMoney(drawer->getOutAmount(), QString(), 2));
    lines.append(line);
    line = QString(" %1 %2").arg(KGlobal::locale()->formatMoney(drawer->getAvailableInCash(), QString(), 2)).arg(strInDrawerH);
    lines.append(line);
    //Now, add a transactions report per user and for today.
    //At this point, drawer must be initialized and valid.
    line = QString("----------%1----------").arg(strTitleTrans);
    lines.append(line);
    line = QString("%1           %2      %3").arg(strId).arg(strAmount).arg(strPaidWith);
    lines.append(line);
    lines.append("----------  ----------  ----------");
    QList<qulonglong> transactionsByUser = drawer->getTransactionIds();
    QStringList trList;

    qDebug()<<"# of transactions:"<<transactionsByUser.count();
    //This gets all transactions ids done since last corteDeCaja.
    Azahar *myDb = new Azahar;
    myDb->setDatabase(db);
    for (int i = 0; i < transactionsByUser.size(); ++i) {
      qDebug()<<"i="<<i<<" tr # "<<transactionsByUser.at(i);
      qulonglong idNum = transactionsByUser.at(i);
      TransactionInfo info;
      info = myDb->getTransactionInfo(idNum);
      QString dPayMethod;

      //check if its completed and not cancelled
      if (info.state != tCompleted) {
        qDebug()<<"Excluding from balance a transaction marked as state:"<<info.state;
        continue;
      }

      dId       = QString::number(info.id);
      dAmount   = QString::number(info.amount);
      dHour     = info.time.toString("hh");
      dMinute   = info.time.toString("mm");
      dPaidWith = QString::number(info.paywith);

      QString tmp = QString("%1|%2|%3|%4")
        .arg(dId)
        .arg(dHour+":"+dMinute)
        .arg(KGlobal::locale()->formatMoney(info.amount, QString(), 2))
        .arg(KGlobal::locale()->formatMoney(info.paywith, QString(), 2));

      while (dId.length()<10) dId = dId.insert(dId.length(), ' ');
      while (dAmount.length()<14) dAmount = dAmount.insert(dAmount.length(), ' ');
      while ((dHour+dMinute).length()<6) dMinute = dMinute.insert(dMinute.length(), ' ');
      while (dPaidWith.length()<10) dPaidWith = dPaidWith.insert(dPaidWith.length(), ' ');


      //NOTE: Apr 14 2011: We want to save space on the tickets (receipt printers) and for this we will remove the 'CASH'
      //                   and other strings like that. Instead we will use an asterisk to indicate when a payment is not made in CASH.
      if (info.paymethod != pCash)
          dPayMethod = " **";
      //dPayMethod = myDb->getPayTypeStr(info.paymethod);//using payType methods
      line = QString("%1 %2 %3")
        .arg(dId)
        //.arg(dHour)
        //.arg(dMinute)
        .arg(dAmount)
        //.arg(dPaidWith);
        .arg(dPayMethod);
      lines.append(line);
      line = QString("<tr><td>%1</td><td>%2:%3</td><td>%4</td><td>%5</td><td>%6</td></tr>")
        .arg(dId)
        .arg(dHour)
        .arg(dMinute)
        .arg(dAmount)
        .arg(dPaidWith)
        .arg(dPayMethod);
      linesHTML.append(line);
      tmp += "|"+dPayMethod;
      trList.append( tmp );
    }
    pbInfo.trList = trList;

    //get CashOut list and its info...
    QStringList cfList;
    cfList.clear();
    QList<CashFlowInfo> cashflowInfoList = myDb->getCashFlowInfoList( drawer->getCashflowIds() );
    foreach(CashFlowInfo cfInfo, cashflowInfoList) {
        QString amountF = KGlobal::locale()->formatMoney(cfInfo.amount);
        //QDateTime dateTime; dateTime.setDate(cfInfo.date); dateTime.setTime(cfInfo.time);
        QString dateF   = KGlobal::locale()->formatTime(cfInfo.time);
        QString typeSign; /*cfInfo.typeStr*/
        if (cfInfo.type == ctCashIn || cfInfo.type == ctCashInReservation )
            typeSign = "+";
        else //TODO: IN THE FUTURE REVIEW THIS CODE, IF ADDING MORE CASH IN/OUT CASES.
            typeSign = "-";
        QString data = QString::number(cfInfo.id) + "|" + typeSign + "|" + cfInfo.reason + "|" + amountF + "|" + dateF;
        cfList.append(data);
    }
    pbInfo.cfList = cfList;

    line = QString("</table></body></html>");
    linesHTML.append(line);

    if (Settings::smallTicketDotMatrix()) {
      //print it on the /dev/lpXX...   send lines to print
      showBalance(linesHTML);
      if (Settings::printBalances()) printBalance(lines);
    } else if (Settings::printBalances()) {
      //print it on cups... send pbInfo instead
      QPrinter printer;
      printer.setFullPage( true );
      QPrintDialog printDialog( &printer );
      printDialog.setWindowTitle(i18n("Print Balance"));
      if ( printDialog.exec() ) {
	printer.setPageMargins(0,0,0,0,QPrinter::Millimeter);
	printer.setPaperSize(QSizeF(72,200), QPrinter::Millimeter); //setting small ticket paper size. 72mm x 200mm
	//TODO: Set Copies: printer.setCopyCount(2); //NOTE:Introduced in Qt 4.7 --WARNING-- See also setCollateCopies()
        PrintCUPS::printSmallBalance(pbInfo, printer);
      } else {
          //NOTE: This is a proposition:
          //      If the dialog is accepted (ok), then we print what the user choosed. Else, we print to a file (PDF).
          //      The user can press ENTER when dialog appearing if the desired printer is the default (or the only).
          qDebug()<<"User cancelled printer dialog. We export ticket to a file.";
          QString fn = QString("%1/lemon-printing/").arg(QDir::homePath());
          QDir dir;
          if (!dir.exists(fn))
              dir.mkdir(fn);
          fn = fn+QString("balance-%1__%2.pdf").arg(currentBalanceId).arg(QDateTime::currentDateTime().toString("dd-MMM-yy"));
          qDebug()<<fn;
          
          printer.setOutputFileName(fn);
          printer.setPageMargins(0,0,0,0,QPrinter::Millimeter);
          printer.setPaperSize(QSizeF(72,200), QPrinter::Millimeter); //setting small ticket paper size. 72mm x 200mm
          
          PrintCUPS::printSmallBalance(pbInfo, printer);
      }
    }
    ///NOTE: Really startoperation at this moment? why not wait user request it? Dec 23 2009
    /// slotDoStartOperation(false);

    //for sessions, clear currentBalanceId
    currentBalanceId = 0; //this will make at startOperations to create a new one.
    operationStarted = false;

    delete myDb;
  } //if doit
}

void lemonView::endOfDay() {
  bool doit = false;
  //ASK for security if no lowSecurityMode.
  if (Settings::lowSecurityMode()) {
    doit = true;
  } else {
    dlgPassword->show();
    dlgPassword->clearLines();
    dlgPassword->hide();
    doit = dlgPassword->exec();
  }//else lowsecurity

  if (doit) {
    Azahar *myDb = new Azahar;
    myDb->setDatabase(db);
    QString authBy = dlgPassword->username();
    if (authBy.isEmpty()) authBy = myDb->getUserName(1); //default admin.
    log(loggedUserId, QDate::currentDate(), QTime::currentTime(), i18n("End of Day report printed by %1 at terminal %2 on %3",authBy,Settings::editTerminalNumber(),QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm")));

    // Get every transaction from all day, calculate sales, profit, and profit margin (%). From the same terminal
    qDebug()<<" [******* End of Day Report ******* ]";
    AmountAndProfitInfo amountProfit;
    PrintEndOfDayInfo pdInfo;
    QList<TransactionInfo> transactionsList;
    QPixmap logoPixmap;
    logoPixmap.load(Settings::storeLogo());


    amountProfit     = myDb->getDaySalesAndProfit(Settings::editTerminalNumber());
    transactionsList = myDb->getDayTransactions(Settings::editTerminalNumber());

    pdInfo.storeName = Settings::editStoreName();
    pdInfo.storeAddr = Settings::storeAddress();
    pdInfo.storeLogo = logoPixmap;
    pdInfo.thTitle   = i18n("End of day report");
    pdInfo.thTicket  = i18n("Id");
    pdInfo.salesPerson = loggedUserName;
    pdInfo.terminal  = i18n("at terminal # %1",Settings::editTerminalNumber());
    pdInfo.thDate    = KGlobal::locale()->formatDateTime(QDateTime::currentDateTime(), KLocale::LongDate);
    pdInfo.thTime    = i18n("Time");
    pdInfo.thAmount  = i18n("Amount");
    pdInfo.thProfit  = i18n("Profit");
    pdInfo.thPayMethod = i18n("Method");
    pdInfo.thTotalTaxes= i18n("Total taxes collected for this terminal: ");
    pdInfo.logoOnTop = Settings::chLogoOnTop();
    pdInfo.thTotalSales  = KGlobal::locale()->formatMoney(amountProfit.amount, QString(), 2);
    pdInfo.thTotalProfit = KGlobal::locale()->formatMoney(amountProfit.profit, QString(), 2);

    QStringList lines;
    lines.append(pdInfo.thTitle);
    lines.append(pdInfo.thDate);
    lines.append(loggedUserName+pdInfo.terminal);
    lines.append(pdInfo.thTicket+"    "+pdInfo.thTime+ pdInfo.thAmount+"   "+pdInfo.thProfit+"   "+pdInfo.thPayMethod);
    //lines.append();

    //each transaction...
    double tTaxes = 0;
    for (int i = 0; i < transactionsList.size(); ++i)
    {
      QLocale localeForPrinting; // needed to convert double to a string better
      TransactionInfo info = transactionsList.at(i);
      //qDebug()<<" ========== transactions on end of day: i="<<i<<" ID:"<<info.id<<" amount:"<<info.amount<<" profit:"<<info.utility;
      QString tid      = QString::number(info.id);
      QString hour     = info.time.toString("hh:mm");
      QString amount   =  localeForPrinting.toString(info.amount,'f',2);
      QString profit   =  localeForPrinting.toString(info.utility, 'f', 2);
      QString payMethod;
      payMethod        = myDb->getPayTypeStr(info.paymethod);//using payType methods

      QString line     = tid +"|"+ hour +"|"+ amount +"|"+ profit +"|"+ payMethod;
      pdInfo.trLines.append(line);
      lines.append(tid+"  "+hour+"  "+ amount+"  "+profit+"  "+payMethod);
      tTaxes += info.totalTax;
      qDebug()<<"total sale:"<<info.amount<<" taxes this sale:"<<info.totalTax<<" accumulated taxes:"<<tTaxes;
    } //for each item

    lines.append(i18n("Total Sales : %1",pdInfo.thTotalSales));
    lines.append(i18n("Total Profit: %1",pdInfo.thTotalProfit));

    //add taxes amount
    pdInfo.thTotalTaxes += KGlobal::locale()->formatMoney(tTaxes, QString(), 2);


    if (Settings::smallTicketDotMatrix()) {
      QString printerFile=Settings::printerDevice();
      if (printerFile.length() == 0) printerFile="/dev/lp0";
      QString printerCodec=Settings::printerCodec();
      qDebug()<<"[Printing report on "<<printerFile<<"]";
      qDebug()<<lines.join("\n");
      PrintDEV::printSmallBalance(printerFile, printerCodec, lines.join("\n"));
    } else if (Settings::smallTicketCUPS()) {
      qDebug()<<"[Printing report on CUPS small size]";
      QPrinter printer;
      printer.setFullPage( true );
      QPrintDialog printDialog( &printer );
      printDialog.setWindowTitle(i18n("Print end of day report"));
      if ( printDialog.exec() ) {
        PrintCUPS::printSmallEndOfDay(pdInfo, printer);
      } else {
          //NOTE: This is a proposition:
          //      If the dialog is accepted (ok), then we print what the user choosed. Else, we print to a file (PDF).
          //      The user can press ENTER when dialog appearing if the desired printer is the default (or the only).
          qDebug()<<"User cancelled printer dialog. We export ticket to a file.";
          QString fn = QString("%1/lemon-printing/").arg(QDir::homePath());
          QDir dir;
          if (!dir.exists(fn))
              dir.mkdir(fn);
          fn = fn+QString("endOfDay__%1.pdf").arg(QDateTime::currentDateTime().toString("dd-MMM-yy"));
          qDebug()<<fn;
          
          printer.setOutputFileName(fn);
          printer.setPageMargins(0,0,0,0,QPrinter::Millimeter);
          printer.setPaperSize(QSizeF(72,200), QPrinter::Millimeter); //setting small ticket paper size. 72mm x 200mm
          
          PrintCUPS::printSmallEndOfDay(pdInfo, printer);
      }
    } else { //big printer
      qDebug()<<"[Printing report on CUPS big size]";
      QPrinter printer;
      printer.setFullPage( true );
      QPrintDialog printDialog( &printer );
      printDialog.setWindowTitle(i18n("Print end of day report"));
      if ( printDialog.exec() ) {
        PrintCUPS::printBigEndOfDay(pdInfo, printer);
      }
    }
    delete myDb;
  }
}


void lemonView::showBalance(QStringList lines)
{
  if (Settings::showDialogOnPrinting())
  {
    BalanceDialog *popup = new BalanceDialog(this, lines.join("\n"));
    popup->show();
    popup->hide();
    int result = popup->exec();
    if (result) {
      //qDebug()<<"exec=true";
    }
  }
}

void lemonView::printBalance(QStringList lines)
{
  //Balances are print on small tickets. Getting the selected printed from config.
  if (Settings::printBalances()) {
    if (Settings::smallTicketDotMatrix()) {
      QString printerFile=Settings::printerDevice();
      if (printerFile.length() == 0) printerFile="/dev/lp0";
      QString printerCodec=Settings::printerCodec();
      qDebug()<<"[Printing balance on "<<printerFile<<"]";
      PrintDEV::printSmallBalance(printerFile, printerCodec, lines.join("\n"));
    } // DOT-MATRIX PRINTER on /dev/lpX
  }
}


/* MODEL Zone */

void lemonView::setupModel()
{
  if (!db.isOpen()) {
    connectToDb();
  }
  else {
    //workaround for a stupid crash: when re-connecting after Config, on setTable crashes.
    //Crashes without debug information.
    if (productsModel->tableName() != "products")
      productsModel->setTable("products");
    productsModel->setEditStrategy(QSqlTableModel::OnRowChange);
    ui_mainview.listView->setModel(productsModel);
    ui_mainview.listView->setResizeMode(QListView::Adjust);

    ui_mainview.listView->setModelColumn(productsModel->fieldIndex("photo"));
    ui_mainview.listView->setViewMode(QListView::IconMode);
    ui_mainview.listView->setGridSize(QSize(170,170));
    ui_mainview.listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui_mainview.listView->setMouseTracking(true); //for the tooltip

    ProductDelegate *delegate = new ProductDelegate(ui_mainview.listView);
    ui_mainview.listView->setItemDelegate(delegate);

    productsModel->select();

    //Categories popuplist
    populateCategoriesHash();
    QHashIterator<QString, int> item(categoriesHash);
    while (item.hasNext()) {
      item.next();
      ui_mainview.comboFilterByCategory->addItem(item.key());
      //qDebug()<<"iterando por el hash en el item:"<<item.key()<<"/"<<item.value();
    }

    ui_mainview.comboFilterByCategory->setCurrentIndex(0);
    connect(ui_mainview.comboFilterByCategory,SIGNAL(currentIndexChanged(int)), this, SLOT( setFilter()) );
    connect(ui_mainview.editFilterByDesc,SIGNAL(textEdited(const QString &)), this, SLOT( setFilter()) );
    connect(ui_mainview.rbFilterByDesc, SIGNAL(toggled(bool)), this, SLOT( setFilter()) );
    connect(ui_mainview.rbFilterByCategory, SIGNAL(toggled(bool)), this, SLOT( setFilter()) );

    ui_mainview.rbFilterByCategory->setChecked(true);
    setFilter();
  }
 }

void lemonView::populateCategoriesHash()
{
  Azahar * myDb = new Azahar;
  myDb->setDatabase(db);
  categoriesHash = myDb->getCategoriesHash();
  delete myDb;
}

void lemonView::listViewOnMouseMove(const QModelIndex & index)
{
  //NOTE: Problem: here the data on the view does not change. This is because we do not
  //      update this view's data, we modify directly the data at database until we sell a product.
  //      and until that moment we can update this view.
  // UPDATE: We have at productsHash the property qtyOnList, we can use such qty to display available qty.
  //      But if we are working on a network (multiple POS). It will not be true the information.
  QString tprice = i18n("Price: ");
  QString tstock = i18n("Available: ");
  QString tdisc  = i18n("Discount:"); //TODO: Only include if valid until now...
  QString tcategory = i18n("Category:");
  QString tmoreAv = i18n("in stock");
  QString tmoreAv2= i18n("in your shopping cart, Total Available");

  //getting data from model...
  const QAbstractItemModel *model = index.model();
  int row = index.row();
  QModelIndex indx = model->index(row, 1);
  QString desc = model->data(indx, Qt::DisplayRole).toString();
  indx = model->index(row, 2);
  double price = model->data(indx, Qt::DisplayRole).toDouble();
  indx = model->index(row, 3);
  double stockqty = model->data(indx, Qt::DisplayRole).toDouble();
  indx = model->index(row, 0);
  QString code = model->data(indx, Qt::DisplayRole).toString();
  ProductInfo pInfo;
  bool onList=false;
  if (productsHash.contains(code.toULongLong())) {
    pInfo = productsHash.value(code.toULongLong());
    onList = true;
  }

  QString line1 = QString("<p><b><i>%1</i></b><br>").arg(desc);
  QString line2 = QString("<b>%1</b>%2<br>").arg(tprice).arg(KGlobal::locale()->formatMoney(price));
  QString line3;
  if (onList) line3 = QString("<b>%1</b> %2 %5 %6, %3 %7: %4<br></p>").arg(tstock).arg(stockqty).arg(pInfo.qtyOnList).arg(stockqty - pInfo.qtyOnList).arg(pInfo.unitStr).arg(tmoreAv).arg(tmoreAv2);
  else line3 = QString("<b>%1</b> %2 %3 %4<br></p>").arg(tstock).arg(stockqty).arg(pInfo.unitStr).arg(tmoreAv);
  QString text = line1+line2+line3;

  ui_mainview.listView->setToolTip(text);
}

void lemonView::listViewOnClick(const QModelIndex & index)
{
  //getting data from model...
  const QAbstractItemModel *model = index.model();
  int row = index.row();
  QModelIndex indx = model->index(row, 0);
  QString code = model->data(indx, Qt::DisplayRole).toString();
  insertItem(code);
}

//This is done at the end of each transaction...
void lemonView::updateModelView()
{
  //Submit and select causes a flick and costs some milliseconds
  productsModel->submitAll();
  productsModel->select();
}

void lemonView::showProductsGrid(bool show)
{
  if (show) {
    ui_mainview.frameGridView->show();
  }
  else {
    ui_mainview.frameGridView->hide();
  }
}

void lemonView::hideProductsGrid()
{
  ui_mainview.frameGridView->hide();
}

void lemonView::showPriceChecker()
{
  PriceChecker *priceDlg = new PriceChecker(this);
  priceDlg->setDb(db);
  priceDlg->show();
}

void lemonView::setFilter()
{
  //NOTE: This is a QT BUG.
  //   If filter by description is selected and the text is empty, and later is re-filtered
  //   then NO pictures are shown; even if is refiltered again.
  QRegExp regexp = QRegExp(ui_mainview.editFilterByDesc->text());
  
  if (ui_mainview.rbFilterByDesc->isChecked()) { //by description
      if (!regexp.isValid())  ui_mainview.editFilterByDesc->setText("");
      if (ui_mainview.editFilterByDesc->text()=="*" || ui_mainview.editFilterByDesc->text()=="")
        productsModel->setFilter("products.isARawProduct=false");
      else
        productsModel->setFilter(QString("products.isARawProduct=false and products.name REGEXP '%1'").arg(ui_mainview.editFilterByDesc->text()));
  }
  else {
    if (ui_mainview.rbFilterByCategory->isChecked()) { //by category
      //Find catId for the text on the combobox.
      int catId=-1;
      QString catText = ui_mainview.comboFilterByCategory->currentText();
      if (categoriesHash.contains(catText)) {
        catId = categoriesHash.value(catText);
      }
      productsModel->setFilter(QString("products.isARawProduct=false and products.category=%1").arg(catId));
    } else { //by most sold products in current month --biel
      productsModel->setFilter("products.isARawProduct=false and (products.datelastsold > ADDDATE(sysdate( ), INTERVAL -31 DAY )) ORDER BY products.datelastsold DESC"); //limit or not the result to 5?
      
      //products.code IN (SELECT * FROM (SELECT product_id FROM (SELECT product_id, sum( units ) AS sold_items FROM transactions t, transactionitems ti WHERE  t.id = ti.transaction_id AND t.date > ADDDATE( sysdate( ) , INTERVAL -31 DAY ) GROUP BY ti.product_id) month_sold_items ORDER BY sold_items DESC LIMIT 5) popular_products)
    }
  }
  productsModel->select();
}

void lemonView::setupDB()
{
  qDebug()<<"Setting up database...";
  if (db.isOpen()) db.close();
  //QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
  //db = QSqlDatabase::addDatabase("QMYSQL");
  db.setHostName(Settings::editDBServer());
  db.setDatabaseName(Settings::editDBName());
  db.setUserName(Settings::editDBUsername());
  db.setPassword(Settings::editDBPassword());
  connectToDb();
}

void lemonView::connectToDb()
{
  if (!db.open()) {
    db.open(); //try to open connection
    qDebug()<<"(1/connectToDb) Trying to open connection to database..";
  }
  if (!db.isOpen()) {
    db.open(); //try to open connection again...
    qDebug()<<"(2/connectToDb) Trying to open connection to database..";
  }
  if (!db.isOpen()) {
    qDebug()<<"(3/connectToDb) Configuring..";
    emit signalShowDbConfig();
  } else {
    //finally, when connection stablished, setup all models.
    if (!modelsCreated) { //Create models...
      productsModel       = new QSqlTableModel();
      historyTicketsModel = new QSqlRelationalTableModel();
      modelsCreated = true;
    }
    setupModel();
    setupHistoryTicketsModel();
    setupClients();
    //pass db to login/pass dialogs
    dlgLogin->setDb(db);
    dlgPassword->setDb(db);

    //checking if is the first run.
    Azahar *myDb = new Azahar;
    myDb->setDatabase(db);
    if (myDb->getConfigFirstRun())  syncSettingsOnDb();
    delete myDb;
  }
}

void lemonView::setupClients()
{
  qDebug()<<"Setting up clients...";
  ClientInfo info;
  QString mainClient;
  clientsHash.clear();
  ui_mainview.comboClients->clear();
  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  clientsHash = myDb->getClientsHash();
  mainClient  = myDb->getMainClient();

    //Set by default the 'general' client.
    QHashIterator<QString, ClientInfo> i(clientsHash);
    while (i.hasNext()) {
      i.next();
      info = i.value();
      ui_mainview.comboClients->addItem(info.name);
    }

    int idx = ui_mainview.comboClients->findText(mainClient,Qt::MatchCaseSensitive);
    if (idx>-1) ui_mainview.comboClients->setCurrentIndex(idx);
    clientInfo = clientsHash.value(mainClient);
    updateClientInfo();
    refreshTotalLabel();

    delete myDb;
}

void lemonView::comboClientsOnChange()
{
  if ( !specialOrders.isEmpty() ) {
    // There are special orders, from now, we cannot change client
    updateClientInfo();
    refreshTotalLabel();
    return;
    //maybe the client combo box is changed, but not the data (points, discount...)
  }
  QString newClientName    = ui_mainview.comboClients->currentText();
  qDebug()<<"Client info changed by user.";
  if (clientsHash.contains(newClientName)) {
    clientInfo = clientsHash.value(newClientName);
    updateClientInfo();
    refreshTotalLabel();
    ui_mainview.editItemCode->setFocus();
  }
}

void lemonView::updateClientInfo()
{
  QString dStr;
  if (clientInfo.discount >0) {
      double discMoney = (clientInfo.discount/100)*totalSumWODisc;
      dStr = i18n("Discount: %1%  [%2]",clientInfo.discount, KGlobal::locale()->formatMoney(discMoney));
  } else if (oDiscountMoney >0 ){
      dStr = i18n("Discount: 1% ", KGlobal::locale()->formatMoney(oDiscountMoney));
  }
  QString pStr = i18n("%1 points", clientInfo.points);
  //QString dStr = i18n("Discount: %1% ",clientInfo.discount);
  //double discMoney = (clientInfo.discount/100)*totalSumWODisc;
  //QString frmDisc = i18n("[%1]", KGlobal::locale()->formatMoney(discMoney));
  //dStr = dStr + "\n"+KGlobal::locale()->formatMoney(discMoney);
  ui_mainview.lblClientDiscount->setText(dStr);
  ui_mainview.labelClientDiscounted->setText(pStr);
  QPixmap pix;
  pix.loadFromData(clientInfo.photo);
  ui_mainview.lblClientPhoto->setPixmap(pix);
  qDebug()<<"Updating client info...";
}

void lemonView::setHistoryFilter() {
  historyTicketsModel->setFilter(QString("date <= STR_TO_DATE('%1', '%d/%m/%Y')").
    arg(ui_mainview.editTicketDatePicker->date().toString("dd/MM/yyyy")));
  historyTicketsModel->setSort(historyTicketsModel->fieldIndex("id"),Qt::DescendingOrder); //change this when implemented headers click
}

void lemonView::setupHistoryTicketsModel()
{
  //qDebug()<<"Db name:"<<db.databaseName()<<", Tables:"<<db.tables();
  if (historyTicketsModel->tableName().isEmpty()) {
    if (!db.isOpen()) db.open();
    historyTicketsModel->setTable("v_transactions");
    historyTicketsModel->setRelation(historyTicketsModel->fieldIndex("clientid"), QSqlRelation("clients", "id", "name"));
    historyTicketsModel->setRelation(historyTicketsModel->fieldIndex("userid"), QSqlRelation("users", "id", "username"));
    
    historyTicketsModel->setHeaderData(historyTicketsModel->fieldIndex("id"), Qt::Horizontal, i18n("Tr"));
    historyTicketsModel->setHeaderData(historyTicketsModel->fieldIndex("clientid"), Qt::Horizontal, i18n("Client"));
    historyTicketsModel->setHeaderData(historyTicketsModel->fieldIndex("datetime"), Qt::Horizontal, i18n("Date"));
    historyTicketsModel->setHeaderData(historyTicketsModel->fieldIndex("userid"), Qt::Horizontal, i18n("User"));
    historyTicketsModel->setHeaderData(historyTicketsModel->fieldIndex("itemcount"), Qt::Horizontal, i18n("Items"));
    historyTicketsModel->setHeaderData(historyTicketsModel->fieldIndex("amount"), Qt::Horizontal, i18n("Total"));
    historyTicketsModel->setHeaderData(historyTicketsModel->fieldIndex("disc"), Qt::Horizontal, i18n("Discount"));

    ui_mainview.ticketView->setModel(historyTicketsModel);
    ui_mainview.ticketView->setColumnHidden(historyTicketsModel->fieldIndex("date"), true);
    ui_mainview.ticketView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui_mainview.ticketView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui_mainview.ticketView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui_mainview.ticketView->resizeColumnsToContents();
    ui_mainview.ticketView->setCurrentIndex(historyTicketsModel->index(0, 0));
    
    historyTicketsModel->setSort(historyTicketsModel->fieldIndex("id"),Qt::DescendingOrder);
    historyTicketsModel->select();
  }
  setHistoryFilter();
}

void lemonView::setupTicketView()
{
  if (historyTicketsModel->tableName().isEmpty()) setupHistoryTicketsModel();
  historyTicketsModel->setSort(historyTicketsModel->fieldIndex("id"),Qt::DescendingOrder);
  historyTicketsModel->select();
  QSize tableSize = ui_mainview.ticketView->size();
  int portion = tableSize.width()/7;
  ui_mainview.ticketView->horizontalHeader()->setResizeMode(QHeaderView::Interactive);
  ui_mainview.ticketView->horizontalHeader()->resizeSection(historyTicketsModel->fieldIndex("id"), portion);
  ui_mainview.ticketView->horizontalHeader()->resizeSection(historyTicketsModel->fieldIndex("name"), portion);
  ui_mainview.ticketView->horizontalHeader()->resizeSection(historyTicketsModel->fieldIndex("datetime"), portion);
  ui_mainview.ticketView->horizontalHeader()->resizeSection(historyTicketsModel->fieldIndex("username"), portion);
  ui_mainview.ticketView->horizontalHeader()->resizeSection(historyTicketsModel->fieldIndex("itemcount"), portion);
  ui_mainview.ticketView->horizontalHeader()->resizeSection(historyTicketsModel->fieldIndex("amount"), portion);
  ui_mainview.ticketView->horizontalHeader()->resizeSection(historyTicketsModel->fieldIndex("disc"), portion);
}

void lemonView::itemHIDoubleClicked(const QModelIndex &index){
  if (db.isOpen()) {
    //getting data from model...
    const QAbstractItemModel *model = index.model();
    int row = index.row();
    QModelIndex indx = model->index(row, 1); // id = columna 1
    qulonglong transactionId = model->data(indx, Qt::DisplayRole).toULongLong();
    printTicketFromTransaction(transactionId);
    //return to selling tab
    ui_mainview.mainPanel->setCurrentIndex(pageMain);
  }
}

void lemonView::printSelTicket()
{
  QModelIndex index = ui_mainview.ticketView->currentIndex();
  if (historyTicketsModel->tableName().isEmpty()) setupHistoryTicketsModel();
  if (index == historyTicketsModel->index(-1,-1) ) {
    KMessageBox::information(this, i18n("Please select a ticket to print."), i18n("Cannot print ticket"));
  }
  else  {
    qulonglong id = historyTicketsModel->record(index.row()).value("id").toULongLong();
    //ASK for security if no lowSecurityMode.
    bool doit = false;
    if (Settings::lowSecurityMode()) {
        doit = true;
    } else {
        dlgPassword->show();
        dlgPassword->clearLines();
        dlgPassword->hide();
        doit = dlgPassword->exec();
    }//else lowsecurity
    if (doit)
        printTicketFromTransaction(id);
    else {
        //show a notification.
        qDebug()<<"No administrator password supplied for reprint ticket";
        KNotification *notify = new KNotification("information", this);
        notify->setText(i18n("Reprint ticket cancelled."));
        QPixmap pixmap = DesktopIcon("dialog-error",32);
        notify->setPixmap(pixmap);
        notify->sendEvent();
    }
  }
  //return to selling tab
  ui_mainview.mainPanel->setCurrentIndex(pageMain);
}

void lemonView::printTicketFromTransaction(qulonglong transactionNumber)
{
  QList<TicketLineInfo> ticketLines;
  TicketInfo ticket;
  ticketLines.clear();
  ticket.hasSpecialOrders = false;
  ticket.completingSpecialOrder = false;
  
  if (!db.isOpen()) db.open();
  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  
  TransactionInfo trInfo = myDb->getTransactionInfo(transactionNumber);
  QList<TransactionItemInfo> pListItems = myDb->getTransactionItems(transactionNumber);
  double itemsDiscount=0;
  double soGTotal = 0;
  QDateTime soDeliveryDT;

  //NOTE: Fixing printing reservations that does not exists.
  ReservationInfo rInfo = myDb->getReservationInfoFromTr(transactionNumber);
  //qDebug()<<"ReservationId:"<<rInfo.id<<" TrNum:"<<transactionNumber<<" rInfo.transaction_id"<<rInfo.transaction_id;
  if (rInfo.transaction_id != trInfo.id) { //its not true!
      ticket.reservationId = 0;
      ticket.isAReservation = false;
  } else {
      ticket.reservationId  = rInfo.id;
      ticket.isAReservation = true;
      if ( rInfo.status == rCompleted ) //NOTE: This is not saved in the rInfo, but only completed reservations can be re-printed.
          ticket.reservationStarted = false;
      else
        ticket.reservationStarted = true; 
      ticket.reservationPayment = rInfo.payment;
      ticket.purchaseTotal = rInfo.total;
  } //end fixing...
  
  for (int i = 0; i < pListItems.size(); ++i) {
    TransactionItemInfo trItem = pListItems.at(i);
    // add line to ticketLines
    TicketLineInfo tLineInfo;
    tLineInfo.qty     = trItem.qty;
    tLineInfo.unitStr = trItem.unitStr;
    tLineInfo.desc    = trItem.name;
    tLineInfo.price   = trItem.price;
    tLineInfo.disc    = trItem.disc;
    tLineInfo.total   = trItem.total;
    tLineInfo.payment = trItem.payment;
    tLineInfo.completePayment = trItem.completePayment;
    tLineInfo.isGroup = trItem.isGroup;
    tLineInfo.deliveryDateTime = trItem.deliveryDateTime;
    tLineInfo.tax     = trItem.tax;
    itemsDiscount    += tLineInfo.disc;

    double gtotal     = trItem.total + trItem.tax;
    tLineInfo.gtotal  =  Settings::addTax()  ? gtotal : tLineInfo.total;
    soGTotal         += tLineInfo.gtotal;
    soDeliveryDT      = trItem.deliveryDateTime; // this will be the same for all the SO, so it does not matter if overwrited.
    
    qDebug()<<"\n*** item discount:"<<tLineInfo.disc<<" total itemsDiscount:"<<itemsDiscount<<"\n";
    qDebug()<<"\n*** soGTotal:"<<soGTotal<<" deliveryDT:"<<soDeliveryDT<<"\n";
    QString newName;
    newName = trItem.soId;
    qulonglong sorderid = newName.remove(0,3).toULongLong();
    QString    soNotes  = myDb->getSONotes(sorderid);
    soNotes = soNotes.replace("\n", "|  ");
    if (sorderid > 0) {
      ticket.hasSpecialOrders = true;
      ticket.completingSpecialOrder = false; //we are re-printing...
      QList<ProductInfo> pList = myDb->getSpecialOrderProductsList(sorderid);
      newName = "";
      foreach(ProductInfo info, pList ) {
        QString unitStr;
        if (info.units == 1 ) unitStr=" "; else unitStr = info.unitStr;
        newName += "|  " + QString::number(info.qtyOnList) + " "+ unitStr +" "+ info.desc;
      }
      tLineInfo.geForPrint = trItem.name+newName+"|  |"+i18n("Notes:")+soNotes+" | ";
    } else tLineInfo.geForPrint = "";

    //qDebug()<<"isGROUP:"<<trItem.isGroup;
    if (trItem.isGroup) {
      tLineInfo.geForPrint = trItem.name;
      QString n = trItem.name.section('|',0,0);
      trItem.name = n;
      tLineInfo.desc    = trItem.name;
    }
    
    ticketLines.append(tLineInfo);
  }
  
  //Ticket
  QDateTime dt;
  dt.setDate(trInfo.date);
  dt.setTime(trInfo.time);
  ticket.clientid = trInfo.clientid;
  ticket.datetime = dt;
  ticket.number = transactionNumber;
  ticket.total  = trInfo.amount;
  ticket.change = trInfo.changegiven;
  ticket.paidwith = trInfo.paywith;
  ticket.itemcount = trInfo.itemcount;
  if (!trInfo.cardnumber.isEmpty())
    ticket.cardnum = trInfo.cardnumber.replace(0,15,"***************"); //FIXED: Only save last 4 digits
  else
    ticket.cardnum  = "";
  ticket.cardAuthNum = trInfo.cardauthnum;
  ticket.paidWithCard = (trInfo.paymethod == 2) ? true:false;
  ticket.clientDisc = trInfo.disc;
  ticket.clientDiscMoney = trInfo.discmoney;
  ticket.buyPoints = trInfo.points;
  ticket.clientPoints = myDb->getClientInfo(ticket.clientid).points;
  ticket.lines = ticketLines;
  ticket.terminal = QString::number(trInfo.terminalnum);
  ticket.totalTax = trInfo.totalTax;

  double subtotal = ticket.total + itemsDiscount + trInfo.discmoney; // - trInfo.totaltax;
  if (Settings::addTax())
    subtotal  = subtotal;
  else
    subtotal  = subtotal - ticket.totalTax;
  QString realSubtotal = KGlobal::locale()->formatMoney(subtotal, QString(), 2);

  qDebug()<<"\n*** Ticket tax:"<<trInfo.totalTax<<" itemsDiscount:"<<itemsDiscount<<"client Discount:"<<trInfo.discmoney<<" ticket total:"<<ticket.total<<" SUBTOTAL:"<<subtotal<<" AddTax:"<<Settings::addTax()<<" \n";
  ticket.subTotal = realSubtotal;
  if (ticket.hasSpecialOrders) ticket.deliveryDT = soDeliveryDT;
  ticket.soTotal = soGTotal;
  
  printTicket(ticket);
  delete myDb;
}

void lemonView::showReprintTicket()
{
  ui_mainview.mainPanel->setCurrentIndex(pageReprintTicket);
  QTimer::singleShot(500, this, SLOT(setupTicketView()));
}

void lemonView::cashOut()
{
  bool doit = false;
  //ASK for security if no lowSecurityMode.
  if (Settings::lowSecurityMode()) {
    doit = true;
  } else {
    dlgPassword->show();
    dlgPassword->clearLines();
    dlgPassword->hide();
    doit = dlgPassword->exec();
  }//else lowsecurity
  
  if (doit) {
    double max = drawer->getAvailableInCash();
    if (!max>0) {
      //KPassivePopup::message( i18n("Error:"),i18n("Cash not available at drawer!"),DesktopIcon("dialog-error", 48), this );

      KNotification *notify = new KNotification("information", this);
      notify->setText(i18n("Cash not available at drawer!"));
      QPixmap pixmap = DesktopIcon("dialog-error",32);
      notify->setPixmap(pixmap);
      notify->sendEvent();

    } else {
      InputDialog *dlg = new InputDialog(this, false, dialogCashOut, i18n("Cash Out"), 0.001, max);
      if (dlg->exec() ) {
        Azahar *myDb = new Azahar;
        myDb->setDatabase(db);

        CashFlowInfo info;
        info.amount = dlg->dValue;
        info.reason = dlg->reason;
        info.date = QDate::currentDate();
        info.time = QTime::currentTime();
        info.terminalNum = Settings::editTerminalNumber();
        info.userid = loggedUserId;
        info.type   = ctCashOut; //Normal cash-out
        qulonglong cfId = myDb->insertCashFlow(info);
        //affect drawer
        //NOTE: What about CUPS printers?
        if (Settings::openDrawer() && Settings::smallTicketDotMatrix() && Settings::printTicket() ) drawer->open();
        drawer->substractCash(info.amount);
        drawer->insertCashflow(cfId);
        QString authBy = dlgPassword->username();
        if (authBy.isEmpty()) authBy = myDb->getUserName(1); //default admin.
        log(loggedUserId, QDate::currentDate(), QTime::currentTime(), i18n("Cash-OUT by %1 at terminal %2 on %3",authBy,Settings::editTerminalNumber(),QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm")));
        delete myDb;
      }
    }
  }
}

void lemonView::cashIn()
{
  bool doit = false;
  //ASK for security if no lowSecurityMode.
  if (Settings::lowSecurityMode()) {
    doit = true;
  } else {
    dlgPassword->show();
    dlgPassword->clearLines();
    dlgPassword->hide();
    doit = dlgPassword->exec();
  }//else lowsecurity
  
  if (doit) {
    InputDialog *dlg = new InputDialog(this, false, dialogCashOut, i18n("Cash In"));
    if (dlg->exec() ) {
      Azahar *myDb = new Azahar;
      myDb->setDatabase(db);

      CashFlowInfo info;
      info.amount = dlg->dValue;
      info.reason = dlg->reason;
      info.date = QDate::currentDate();
      info.time = QTime::currentTime();
      info.terminalNum = Settings::editTerminalNumber();
      info.userid = loggedUserId;
      info.type   = ctCashIn; //normal cash-out
      qulonglong cfId = myDb->insertCashFlow(info);
      //affect drawer
      //NOTE: What about CUPS printers?
      if (Settings::openDrawer() && Settings::smallTicketDotMatrix() && Settings::printTicket() ) drawer->open();
      drawer->addCash(info.amount);
      drawer->insertCashflow(cfId);

      QString authBy = dlgPassword->username();
      if (authBy.isEmpty()) authBy = myDb->getUserName(1); //default admin.
      log(loggedUserId, QDate::currentDate(), QTime::currentTime(), i18n("Cash-IN [%1] by %2 at terminal %3 on %4",QString::number(info.amount, 'f',2),authBy,Settings::editTerminalNumber(),QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm")));
      delete myDb;
    }
  }
}

void lemonView::cashAvailable()
{
  double available = drawer->getAvailableInCash();
  KNotification *notify = new KNotification("information", this);
  notify->setText(i18n("There are <b> %1 in cash </b> available at the drawer.", KGlobal::locale()->formatMoney(available)));
  QPixmap pixmap = DesktopIcon("dialog-information",32);
  notify->setPixmap(pixmap);
  notify->sendEvent();
}

void lemonView::log(const qulonglong &uid, const QDate &date, const QTime &time, const QString &text)
{
  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  myDb->insertLog(uid, date, time, "[ LEMON ] "+text);
  delete myDb;
}

/** Inserts a S.O. into the buy list, at 50% of its price (a prepayment).
**  Or it can be the total payment.
**/
void lemonView::addSpecialOrder()
{
  if ( transactionInProgress && (totalSum >0) && specialOrders.isEmpty() ) {
    KNotification *notify = new KNotification("information", this);
    notify->setText(i18n("Please finish the current transaction before creating a special order."));
    QPixmap pixmap = DesktopIcon("dialog-information",32);
    notify->setPixmap(pixmap);
    notify->sendEvent();
    return;
  }

  //first, if the sale contains another SO, then only the same client is allowed, and we must disable the client selection on the SO editor.
  bool allowClientSelection = specialOrders.isEmpty();
  
  SpecialOrderInfo soInfo;
  qulonglong newSOId = 0;
  SpecialOrderEditor *soEditor = new SpecialOrderEditor(this);
  soEditor->setModel(productsModel);
  soEditor->setDb(db);
  soEditor->setTransId(currentTransaction);
  soEditor->setUsername(loggedUserName);
  soEditor->setClientsComboEnabled(allowClientSelection);
  soEditor->setDeliveryDateTimeEnabled(allowClientSelection);
  if (!allowClientSelection) {
    soEditor->setClientName(clientInfo.name);
    soEditor->setDeliveryDateTime(QDateTime::currentDateTime());
  }

  if (soEditor->exec()) {
    //get values from dialog
    soInfo.saleid   = currentTransaction;
    soInfo.name     = soEditor->getDescription();
    soInfo.qty      = soEditor->getQty();
    soInfo.price    = soEditor->getPrice();
    soInfo.cost     = soEditor->getCost();
    soInfo.notes    = soEditor->getNotes();
    soInfo.status   = stPending;
    soInfo.units    = 1; /// MCH 20DIC09
    soInfo.unitStr  = "";
    soInfo.groupElements = soEditor->getGroupElementsStr();
    soInfo.payment  = soEditor->getPayment();
    soInfo.deliveryDateTime = soEditor->getDeliveryDateTime();
    if (soInfo.payment == soInfo.price)
      soInfo.completePayment = true;
    else
      soInfo.completePayment = false;
    
    soInfo.dateTime = soEditor->getDateTime();

    if (soInfo.payment == soInfo.price)
      soInfo.completedOnTrNum = currentTransaction;
    else
      soInfo.completedOnTrNum = 0;
    
    soInfo.clientId = soEditor->getClientId();
    soInfo.userId = soEditor->getUserId();

    Azahar *myDb = new Azahar;
    myDb->setDatabase(db);

    //for the user discount, change user on transaction.
    clientInfo = myDb->getClientInfo(soInfo.clientId);
    int idx = ui_mainview.comboClients->findText(clientInfo.name,Qt::MatchCaseSensitive);
    if (idx>-1) ui_mainview.comboClients->setCurrentIndex(idx);
    updateClientInfo();
    refreshTotalLabel();

    newSOId = myDb->insertSpecialOrder(soInfo); //we need to insert it to get the orderid.
    if ( newSOId == 0 ) qDebug()<<"Error insertando SO :"<<myDb->lastError();

    soInfo.orderid = newSOId;

    //discount from SO elements
    soInfo.disc = myDb->getSpecialOrderAverageDiscount(soInfo.orderid)/100; //in percentage.
    double soDiscount = soInfo.disc * soInfo.payment *soInfo.qty; 

    //add info to the buy list

    int insertedAtRow = -1;
    QString codeX = QString("so.%1").arg(QString::number(soInfo.orderid));
    QString newName = soInfo.name+"\n"+soEditor->getContentNames();
    /// here we insert the product at  its payment - can be 50%  pre-payment
    insertedAtRow = doInsertItem(codeX, newName, soInfo.qty, soInfo.payment, soDiscount, soInfo.unitStr); //April 5 2010: Now SO can have DISCOUNTS on its elements...
    soInfo.insertedAtRow = insertedAtRow;
    newName = newName.replace("\n", "|");
    soInfo.geForPrint = newName;

    //after inserting so in the db, calculate tax.
    soInfo.averageTax = myDb->getSpecialOrderAverageTax(soInfo.orderid);
    //add to the hash
    specialOrders.insert(soInfo.orderid, soInfo);
    refreshTotalLabel();
    //Saving session.
    qDebug()<<"** INSERTING A SPECIAL ORDER [updating balance/transaction]";
    updateBalance(false);
    updateTransaction();

    //disable client combo box.
    ui_mainview.comboClients->setDisabled(true);
    
    delete myDb;
  }
  //finally delete de ui
  delete soEditor;
}

void lemonView::specialOrderComplete()
{
  //first ensure we have no pending transaction
  if ( transactionInProgress && (totalSum >0) ) {
    KNotification *notify = new KNotification("information", this);
    notify->setText(i18n("Please finish the current transaction before completing a special order."));
    QPixmap pixmap = DesktopIcon("dialog-information",32);
    notify->setPixmap(pixmap);
    notify->sendEvent();
    return;
  }

  SOSelector *dlg = new SOSelector(this);
  dlg->setDb(db);
  
  if (dlg->exec() ) {
    qulonglong tNum=dlg->getSelectedTicket();
    Azahar *myDb = new Azahar;
    myDb->setDatabase(db);
    QList<SpecialOrderInfo> soList = myDb->getAllSOforSale(tNum);
    if (soList.isEmpty()) {
      KNotification *notify = new KNotification("information", this);
      notify->setText(i18n("The given ticket number does not contains any special order."));
      QPixmap pixmap = DesktopIcon("dialog-information",32);
      notify->setPixmap(pixmap);
      notify->sendEvent();
      return;
    }
    //continue.. its time to complete
    QStringList paidOrders; paidOrders << i18n("These special orders cannot be completed because:");
    int soCompletePayments = 0;
    qulonglong clientIdForDiscount = 0;
    foreach(SpecialOrderInfo soInfo, soList) {
      if ( soInfo.status == stDelivered || soInfo.status == stCancelled) {
        QString stStr;
        if (soInfo.status == stCancelled)
          stStr = i18n("<b>is Cancelled</b>");
        else
          stStr = i18n("is already <b>Delivered</b>");
        paidOrders << i18n("%1 %2.", soInfo.name, stStr);
      } else {
        //first check if the so is already delivered or cancelled
        if (soInfo.status == stDelivered || soInfo.status == stCancelled) {
          continue; //HEY PURIST, WHEN I GOT SOME TIME I WILL CLEAN IT
        }
        if (soInfo.payment == soInfo.price) {
          soCompletePayments++;
          myDb->specialOrderSetStatus(soInfo.orderid, stDelivered);
          qDebug()<<"This special order is completeley paid and marked as delivered without emiting a ticket.";
          KNotification *notify = new KNotification("information", this);
          notify->setText(i18n("The special order %1 in ticket %2 is completely paid. Marked as delivered.", soInfo.orderid, soInfo.saleid));
          QPixmap pixmap = DesktopIcon("dialog-information",32);
          notify->setPixmap(pixmap);
          notify->sendEvent();
          continue; //dont insert this...
        }
        qDebug()<<"Going to insert so in the list.";
        clientIdForDiscount = soInfo.clientId;
        //insert each so to the list.
        int insertedAtRow = -1;
        QString codeX = QString("so.%1").arg(QString::number(soInfo.orderid));

        QList<ProductInfo> pList = myDb->getSpecialOrderProductsList(soInfo.orderid);
        QString newName = soInfo.name;
        foreach(ProductInfo inf, pList) {
          QString unitStr;
          if (inf.units == 1 ) unitStr=" "; else unitStr = inf.unitStr;
          newName += "\n  " + QString::number(inf.qtyOnList) + " "+ unitStr +" "+ inf.desc;
        }
        newName = newName+"\n"+i18n("Notes:")+soInfo.notes;

        ///discount from SO elements
        double toPay = soInfo.price-soInfo.payment;
        double soDiscount = soInfo.disc * toPay * soInfo.qty;

        /// here we insert the product with the appropiate payment.
        insertedAtRow = doInsertItem(codeX, newName, soInfo.qty, toPay, soDiscount, soInfo.unitStr);
        //modify SpecialOrder info for database update.
        soInfo.insertedAtRow = insertedAtRow;
        soInfo.payment = soInfo.price-soInfo.payment; //the final payment is what we save on db.
        soInfo.completePayment = true;
        soInfo.status  = stReady; //status = ready to deliver.
        soInfo.completedOnTrNum = currentTransaction;
        newName = newName.replace("\n", "|");
        soInfo.geForPrint = newName;

        ///after inserting so in the db, calculate tax.
        soInfo.averageTax = myDb->getSpecialOrderAverageTax(soInfo.orderid);
        
        //add to the hash
        specialOrders.insert(soInfo.orderid, soInfo);
        refreshTotalLabel();
      } //else if cancelled or delivered
    } //foreach soInfo

    if (clientIdForDiscount == 0) {
      // no client id.. this happens on completeley paid orders.
      clientInfo = clientsHash.value(myDb->getMainClient());
      clientIdForDiscount = clientInfo.id;
    } else  clientInfo = myDb->getClientInfo(clientIdForDiscount);
    int idx = ui_mainview.comboClients->findText(clientInfo.name,Qt::MatchCaseSensitive);
    if (idx>-1) ui_mainview.comboClients->setCurrentIndex(idx);

    // See if there was an occasional discount on the originating transaction to apply it to the PROFIT.
    lastDiscount = 0;
    lastDiscount = myDb->getTransactionDiscMoney(tNum);
    qDebug()<<" Originating transaction discount:"<<lastDiscount<<" applying it to the profit.";
    
    updateClientInfo();
    refreshTotalLabel();
    
    if (paidOrders.count()> 1) { // the first is the pre-message
      KNotification *notify = new KNotification("information", this);
      notify->setText(paidOrders.join("\n"));
      QPixmap pixmap = DesktopIcon("dialog-information",32);
      notify->setPixmap(pixmap);
      notify->sendEvent();
    }
    //Saving session.
    qDebug()<<"** COMPLETING A SPECIAL ORDER [updating balance/transaction]";
    updateBalance(false);
    updateTransaction();
    
    //disable clients combo box
    ui_mainview.comboClients->setDisabled(true);

    delete myDb;
  }
}


void lemonView::lockScreen()
{
  //To allow cashier to suspend sales for a moment. There is still a concept to implement: save uncompleted sales to allow retake later on (minutes, hours, days).

  emit signalDisableUI();
  emit signalDisableLogin();
  QString msg = i18n("<b>This terminal is locked.</b> <br><i>Please enter the user's password to unlock it</i>.");
  lockDialog->showDialog(msg);

  //Saving session.
  qDebug()<<"** LOCKING SCREEN [updating balance/transaction]";
  updateBalance(false);
  updateTransaction();
}

void lemonView::unlockScreen()
{
  //get password from dialog.
  QString pwd = lockDialog->getPassword();
  if (!pwd.isEmpty()) {
    //get user info
    Azahar *myDb = new Azahar;
    myDb->setDatabase(db);
    UserInfo uInfo = myDb->getUserInfo(loggedUserId);
    delete myDb;
    QString givenPass = Hash::password2hash((uInfo.salt+pwd).toLocal8Bit());
    if (givenPass == uInfo.password) {
      //finally close dialog
      lockDialog->hideDialog();
      lockDialog->cleanPassword();
      //unlock ui
      emit signalEnableUI();
      emit signalEnableLogin();
      ui_mainview.editItemCode->setFocus();
    } else {
      lockDialog->cleanPassword();
      lockDialog->shake();
    }
  }
}

//For save sessions

void lemonView::insertBalance()
{
  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  
  //This creates an empty balance
  BalanceInfo info;
  info.id = 0;
  info.dateTimeStart = drawer->getStartDateTime();
  info.dateTimeEnd   = info.dateTimeStart;
  info.userid        = loggedUserId;
  info.username      = loggedUser;
  info.initamount    = drawer->getInitialAmount();
  info.in            = drawer->getInAmount();
  info.out           = drawer->getOutAmount();
  info.cash          = drawer->getAvailableInCash();
  info.card          = drawer->getAvailableInCard();
  info.terminal      = Settings::editTerminalNumber();
  
  info.transactions  = "";
  info.cashflows     = "";
  info.done = false;
  currentBalanceId   = myDb->insertBalance(info);
  qDebug()<<"Inserted the new BALANCE #"<<currentBalanceId;
  delete myDb;
}

void lemonView::updateBalance(bool finish)
{
  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  
  //got info from drawer..
  BalanceInfo info;
  info.id = currentBalanceId;
  info.dateTimeStart = drawer->getStartDateTime();
  info.dateTimeEnd   = QDateTime::currentDateTime();
  info.userid        = loggedUserId;
  info.username      = loggedUser;
  info.initamount    = drawer->getInitialAmount();
  info.in            = drawer->getInAmount();
  info.out           = drawer->getOutAmount();
  info.cash          = drawer->getAvailableInCash();
  info.card          = drawer->getAvailableInCard();
  info.terminal      = Settings::editTerminalNumber();
  info.done          = finish; //only true when finishing the Balace.

  QStringList tmpList;
  foreach(qulonglong tid, drawer->getTransactionIds()) {
    tmpList << QString::number(tid);
  }
  info.transactions  = tmpList.join(",");
  tmpList.clear();

  qDebug()<< __FUNCTION__ <<" Transactions:"<<info.transactions;

  foreach(qulonglong tid, drawer->getCashflowIds()) {
    tmpList << QString::number(tid);
  }
  info.cashflows = tmpList.join(",");
  qDebug()<<"Updating balance #"<<currentBalanceId;
  if (!myDb->updateBalance(info)) qDebug()<<"Error updating balance..";

  delete myDb;
}

void lemonView::updateTransaction()
{
  //fill info
  TransactionInfo info;
  info.id       = currentTransaction;
  info.balanceId= currentBalanceId;
  info.type     = tSell;
  info.amount   = totalSum;
  if (!ui_mainview.groupSaleDate->isHidden())
    info.date     = ui_mainview.editTransactionDate->dateTime().date();
  else
    info.date     = QDate::currentDate();
  info.time     = QTime::currentTime();
  info.paywith  = 0;
  info.changegiven = 0;
  info.paymethod = pCash;
  info.state     = tNotCompleted;
  info.userid    = loggedUserId;
  info.clientid  = clientInfo.id;
  info.cardnumber= "NA";
  info.cardauthnum= "NA";
  info.disc       = clientInfo.discount;
  info.discmoney  = discMoney;
  info.points     = buyPoints;
  info.terminalnum= Settings::editTerminalNumber();
  info.providerid=1;

  double profit = 0;
  double cant   = 0;
  QStringList tmpList;
  foreach(ProductInfo pi, productsHash) {
    profit += (pi.price - pi.cost - pi.disc) * pi.qtyOnList;
    if ( pi.units == uPiece ) cant   += pi.qtyOnList; else cant   += 1;
    tmpList << QString::number(pi.code) + "/" + QString::number(pi.qtyOnList);
  }
  info.itemlist   = tmpList.join(","); //Only save normal products. Its almost DEPRECATED.
  
  tmpList.clear();
  foreach(SpecialOrderInfo soi, specialOrders) {
    profit += (soi.price - soi.cost) * soi.qty;
    if ( soi.units == uPiece ) cant   += soi.qty; else cant   += 1;
    tmpList << QString::number(soi.orderid) + "/" + QString::number(soi.qty);
  }
  info.specialOrders= tmpList.join(",");

  info.itemcount  = cant;
  info.utility    = profit;
  //info.groups     = ""; //DEPRECATED.

  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  myDb->updateTransaction(info);
  delete myDb;
}

void lemonView::suspendSale()
{
  qulonglong count = specialOrders.count() + productsHash.count();
  if ( operationStarted && count>0 ) {
    qulonglong tmpId = currentTransaction;
    qDebug()<<"THE SALE HAS BEEN SUSPENDED. Id="<<tmpId;
    // save transaction and balance
    updateTransaction();
    updateBalance(false);
    // clear widgets
    startAgain();
    //inform the user
    KNotification *notify = new KNotification("information", this);
    notify->setText(i18n("The sale %1 has been sucessfully suspended.", tmpId));
    QPixmap pixmap = DesktopIcon("dialog-information",32);
    notify->setPixmap(pixmap);
    notify->sendEvent();
  }
}

//This will resume the sale, using a new balanceid.
void lemonView::resumeSale()
{
  Azahar *myDb = new Azahar;
  myDb->setDatabase(db);
  ResumeDialog *dlg = new ResumeDialog(this);
  dlg->setUserId(loggedUserId); //note: this must be called before setDb()
  dlg->setDb(db);

  if (dlg->exec()) {
    //get data
    QList<ProductInfo>      pList = dlg->getProductsList();
    QList<SpecialOrderInfo> sList = dlg->getSOList();
    qulonglong trNumber           = dlg->getSelectedTransaction();
    qulonglong clientId           = dlg->getSelectedClient();
    //Check if there is a transaction, and suspend it.
    suspendSale();
    currentTransaction = trNumber;
    emit signalUpdateTransactionInfo();
    clientInfo = myDb->getClientInfo(clientId);
    qDebug()<<"Client id for the resumed sale:";
    int idx = ui_mainview.comboClients->findText(clientInfo.name,Qt::MatchCaseSensitive);
    if (idx>-1) ui_mainview.comboClients->setCurrentIndex(idx);
    updateClientInfo();
    refreshTotalLabel();
    //NOTE: change sale date ?
    //get each product - the availability and group verification will do the insertItem method
    foreach(ProductInfo info, pList) {
      QString qtyXcode = QString::number(info.qtyOnList) + "*" + QString::number(info.code);
      insertItem(qtyXcode);
    }
    foreach(SpecialOrderInfo info, sList) {
      int insertedAtRow = -1;
      QString codeX = QString("so.%1").arg(QString::number(info.orderid));
      //get formated content names for printing/list.
          QStringList list;
          QStringList strlTmp = info.groupElements.split(",");
          foreach(QString str, strlTmp) {
            QString    itemCode = str.section('/',0,0); //.toULongLong();
            double     itemQty  = str.section('/',1,1).toDouble();
            //get item info
            ProductInfo itemInfo = myDb->getProductInfo(itemCode);
            itemInfo.qtyOnList   = itemQty;
            QString unitStr;
            if (itemInfo.units == 1 ) unitStr=""; else unitStr = itemInfo.unitStr;
            list.append("  "+QString::number(itemInfo.qtyOnList)+" "+unitStr+" "+ itemInfo.desc);
          }
          //append NOTES for the SO.
          list.append("\n"+i18n("Notes: %1", info.notes+" \n"));
      //end of formated content names for so
      QString newName = info.name+"\n" + list.join("\n");
      insertedAtRow = doInsertItem(codeX, newName, info.qty, info.payment, 0, info.unitStr);
      info.insertedAtRow = insertedAtRow;
      newName = newName.replace("\n", "|");
      info.geForPrint = newName;
      //change delivery datetime.
      //get original date lapse between so-creation date and delivery date.
      int lap = info.dateTime.date().daysTo( info.deliveryDateTime.date() );
      info.deliveryDateTime = QDateTime::currentDateTime().addDays(lap);
      qDebug()<<"lap:"<<lap;
      //add to the hash
      specialOrders.insert(info.orderid, info);
      //myDb->updateSpecialOrder(info);
      //In case this sale is re-suspended the delivery lapse is going to be increased...
      //this update was moved to finishCurrentTransaction...
    }
    updateBalance(false);
    updateTransaction();
  }
  delete myDb;
}

void lemonView::changeSOStatus()
{
  if ( transactionInProgress && (totalSum >0) ) {
    KNotification *notify = new KNotification("information", this);
    notify->setText(i18n("Please finish the current transaction before changing state for a special order."));
    QPixmap pixmap = DesktopIcon("dialog-information",32);
    notify->setPixmap(pixmap);
    notify->sendEvent();
    return;
  }
  
  SOStatus *dlg = new SOStatus(this);
  dlg->setDb(db);

  if (dlg->exec()) {
    int status         = dlg->getStatusId();
    qulonglong orderid = dlg->getSelectedTicket();

    Azahar *myDb = new Azahar;
    myDb->setDatabase(db);
    myDb->soTicketSetStatus(orderid, status);
    delete myDb;
  }//dlg exec
}


//NOTE: Aug 6 2011: Now occasionalDiscount can be in dollar and cents.
void  lemonView::occasionalDiscount()
{
  bool continuar=false;
  oDiscountMoney = 0;
  if (Settings::lowSecurityMode()) { //qDebug()<<"LOW security mode";
    continuar=true;
  } else {// qDebug()<<"NO LOW security mode";
    dlgPassword->show();
    dlgPassword->hide();
    dlgPassword->clearLines();
    continuar = dlgPassword->exec();
  }
  
  if (continuar) {
    //bool ok=false;
    //double discPercent = 0;
    //double moneyDiscount = 0;
    //InputDialog *dlg = new InputDialog(this, false, dialogMoney, i18n("Enter the discount percentage to apply"), 0.01, 99.0);
    //if (dlg->exec())
    //{
    //  discPercent = dlg->dValue;
    //  ok = true;
    //}
    //SHOW THE NEW DISCOUNT PANEL
    discountPanel->showPanel();
    //the new code is at applyOccasionalDiscount().
  }
}

void lemonView::applyOccasionalDiscount()
{

    ///NOTE: FIXME! The nonDiscountable items should not be discounted with dollar discount or Percentage discount.
    ///             Now the problem is with special Orders. How to allow/disallow discounts for them?
    
    //validate discount: the input has a proper validator.
    if (ui_mainview.rbPercentage->isChecked()) {
        oDiscountMoney = 0; //this is 0 when applying a percentage discount.
        clientInfo.discount = ui_mainview.editDiscount->text().toDouble();
    } else if (ui_mainview.rbMoney->isChecked()) {
        oDiscountMoney = ui_mainview.editDiscount->text().toDouble();
        clientInfo.discount = 0;
    } else if (ui_mainview.rbPriceChange->isChecked()) {
        double priceDiff = 0;
        clientInfo.discount = 0; //reset
        oDiscountMoney = 0; //reset
        ///To change price to an item, it must be selected (obviously already in the purchase list). Only to normal items, no SpecialOrders.
        ///Discount to the whole sale is allowed too.
        if (ui_mainview.tableWidget->currentRow()!=-1 && ui_mainview.tableWidget->selectedItems().count()>4) {
            int row = ui_mainview.tableWidget->currentRow();
            QTableWidgetItem *item = ui_mainview.tableWidget->item(row, colCode);
            qulonglong code = item->data(Qt::DisplayRole).toULongLong();
            if (code <= 0) {
                //it is an special order, not a normal product.
                ///TODO: Implement price change for Special Orders, or is it not a good idea?
            }
            qDebug()<<"Selected code for changing price: "<<code;
            if (code > 0) {
                ProductInfo info = productsHash.take(code); //insert it later...
                ///NOTE: price change will be applied over the normal price. Not applying item discounts. If exists any it will be cleared.
                if (info.isNotDiscountable) {
                    notifierPanel->setSize(350,150);
                    notifierPanel->setOnBottom(false);
                    notifierPanel->showNotification("<b>Cannot change price</b> to product marked as not discountable.",5000);
                    discountPanel->hidePanel();
                    ui_mainview.editItemCode->setFocus();
                    return;
                }
                priceDiff = info.price - ui_mainview.editDiscount->text().toDouble();
                info.disc = priceDiff; // set item discount money. discPercentage will be zero.
                info.discpercentage = 0;
                info.validDiscount = false; ///to detect the difference between offers and price changes.
                productsHash.insert(code, info); //reinsert it with the new discount.
                ///update purchase list!
                updateItem(info);
                //LOG the price change!
                Azahar *myDb = new Azahar;
                myDb->setDatabase(db);
                QString authBy = dlgPassword->username();
                if (authBy.isEmpty()) authBy = myDb->getUserName(1); //default admin.
                log(loggedUserId, QDate::currentDate(), QTime::currentTime(), i18n("Changing price to product %1, from %2 to %3. Authorized by %4", 
                                                                                   code, info.price, ui_mainview.editDiscount->text().toDouble(), authBy));
                delete myDb;
                qDebug()<<QString("Changing price to: %1, from %2 to %3.").arg(code).arg(info.price).arg(ui_mainview.editDiscount->text().toDouble());
            } else {
                // SO cannot be price changed.
                notifierPanel->setSize(350,150);
                notifierPanel->setOnBottom(false);
                notifierPanel->showNotification("<b>Cannot change price</b> to Special Orders, only to normal products.",5000);
            }
        } else {
            notifierPanel->setSize(350,150);
            notifierPanel->setOnBottom(false);
            notifierPanel->showNotification("First <b>select a product</b> to <i>change the price</i> from the sale list.",5000);
        }
        ui_mainview.editItemCode->setFocus();
        refreshTotalLabel();
        ui_mainview.editDiscount->clear();
        discountPanel->hidePanel();
        return; //exit the method, we dont want to run the code at line 4641 and below.
    } else { //by coupon.
        //FIXME:CODE IT!
        oDiscountMoney = 0; //reset
        clientInfo.discount = 0; //reset
    }
    qDebug()<<"Continuing with discount...";
    //clientInfo.discount = discPercent;
    updateClientInfo();
    refreshTotalLabel();
    Azahar *myDb = new Azahar;
    myDb->setDatabase(db);
    QString authBy = dlgPassword->username();
    if (authBy.isEmpty()) authBy = myDb->getUserName(1); //default admin.
    log(loggedUserId, QDate::currentDate(), QTime::currentTime(), i18n("Applying occasional discount. Authorized by %1",authBy));
    delete myDb;

    //At the end, clear the widget. Except for the type.
    ui_mainview.editDiscount->clear();
    discountPanel->hidePanel();
}

//NOTE: Reservations are not treated as sales until they are completed. The amount payment at the reservation is
//      treated as a cash-in, without any transaction implied. The transaction used is kept at NotCompleted state.
void lemonView::reserveItems()
{
    TicketInfo      ticket;
    QList<TicketLineInfo> ticketLines;
    double           payWith = 0.0;
    double           changeGiven = 0.0;
    QString          authnumber = "";
    QString          cardNum = "";
    QString          paidStr = "'[Not Available]'";
    QStringList      groupList;

    startingReservation = true;
    finishingReservation= false;

    reservationPayment = ui_mainview.editAmount->text().toDouble();
    payWith = reservationPayment;
    //change given is allways 0.
    
    //check if there are items in the list. Only on normal products, not SO.
    if (!productsHash.isEmpty()) {
        ReservationInfo rInfo;
        rInfo.id=0;
        rInfo.client_id = clientInfo.id;
        rInfo.transaction_id = currentTransaction;
        rInfo.date = QDate::currentDate();
        rInfo.status = rPending;
        //get Payment Amount.
        rInfo.payment = reservationPayment;
        // total without payment
        rInfo.total = subTotalSum; // This amount is WITHOUT TAXES.
        rInfo.totalTaxes = totalTax;
        qDebug()<< __FUNCTION__ <<" Reservation Amount:"<< rInfo.payment<<" Total :"<<rInfo.total<<" Taxes:"<< rInfo.totalTaxes;

        if (rInfo.payment <= 0) {
            //TODO: Replace this notify with a mibitLineEdit->VIBRAR, y mibitTip
            KNotification *notify = new KNotification("information", this);
            notify->setText(i18n("Please Enter the reservation Amount in the Payment Amount and try again."));
            QPixmap pixmap = DesktopIcon("dialog-information",32);
            notify->setPixmap(pixmap);
            notify->sendEvent();
            ui_mainview.editAmount->setFocus();
            return;
        }

        Azahar *myDb = new Azahar;
        myDb->setDatabase(db);

        double pDiscounts=0;
        double cantidad=0;

        //create the reservation record
        qulonglong rId = myDb->insertReservation(rInfo);

        foreach (ProductInfo pi, productsHash) {
            //Decrement product-qty from inventory.
            if (pi.isAGroup)
                myDb->decrementGroupStock(pi.code, pi.qtyOnList, QDate::currentDate() );
            else
                myDb->decrementProductStock(pi.code, pi.qtyOnList, QDate::currentDate() );
            //get item data
            pDiscounts+= pi.disc * pi.qtyOnList;
            if (pi.units == uPiece) cantidad += pi.qtyOnList; else cantidad += 1; // :)
            QString iname = "";
            if (!pi.groupElementsStr.isEmpty()) {
                QStringList lelem = pi.groupElementsStr.split(",");
                foreach(QString ea, lelem) {
                    if (Settings::printPackContents()) {
                        qulonglong c = ea.section('/',0,0).toULongLong();
                        double     q = ea.section('/',1,1).toDouble();
                        ProductInfo p = myDb->getProductInfo(QString::number(c));
                        QString unitStr;
                        if (p.units == 1 ) unitStr=" "; else unitStr = p.unitStr;
                        iname += "\n  " + QString::number(q) + " "+ unitStr +" "+ p.desc;
                    }
                }
            }
            iname = iname.replace("\n", "|");
            // add line to ticketLines
            TicketLineInfo tLineInfo;
            tLineInfo.qty     = pi.qtyOnList;
            tLineInfo.unitStr = pi.unitStr;
            tLineInfo.isGroup = false;
            if (pi.isAGroup) { 
                tLineInfo.geForPrint    =iname;
                tLineInfo.completePayment = true;
                tLineInfo.payment = 0;
                tLineInfo.isGroup = true;
            }
            tLineInfo.desc    = pi.desc;
            tLineInfo.price   = pi.price;
            tLineInfo.disc    = pi.disc*pi.qtyOnList;
            tLineInfo.total   = (pi.price - pi.disc) * pi.qtyOnList;
            tLineInfo.tax     = pi.totaltax*pi.qtyOnList;
            ticketLines.append(tLineInfo);
            
        } //for each product

        //Register the cash-in
        CashFlowInfo info;
        info.amount = rInfo.payment;
        info.reason = i18n("Reservation #%1 [tr. %2]", rId, currentTransaction);
        info.date = QDate::currentDate();
        info.time = QTime::currentTime();
        info.terminalNum = Settings::editTerminalNumber();
        info.userid = loggedUserId;
        info.type   = ctCashIn;
        qulonglong cfId = myDb->insertCashFlow(info);
        //affect drawer
        if (Settings::openDrawer() && Settings::smallTicketDotMatrix() && Settings::printTicket() ) drawer->open();
        if (ui_mainview.checkCash->isChecked()) {
            drawer->addCash(info.amount);
            drawer->insertCashflow(cfId);
            drawer->substractCash(changeGiven);
            //drawer->incCashTransactions(); //it is not a transaction.
        } else {
            drawer->addCard(payWith);
            drawer->insertCashflow(cfId);
        }

        //write a log 
        QString authBy = dlgPassword->username();
        if (authBy.isEmpty()) authBy = myDb->getUserName(1); //default admin.
            log(loggedUserId, QDate::currentDate(), QTime::currentTime(), i18n("Cash-IN [%1] for RESERVATION [%5] by %2 at terminal %3 on %4",QString::number(info.amount, 'f',2),authBy,Settings::editTerminalNumber(),QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm")).arg(rId));
        //end cash-in

        ui_mainview.editAmount->setStyleSheet("");
        ui_mainview.editCardNumber->setStyleSheet("");

        //TODO:PRINT A TICKET  - Print it twice? one for client other for store (stick it at the product)
        // investigate how to manipulate printer settings (cups) 
        ticket.isAReservation     = true;
        ticket.reservationStarted = true;
        ticket.reservationPayment = rInfo.payment;
        ticket.reservationId  = rId;
        ticket.purchaseTotal  = rInfo.total;
        ticket.datetime = QDateTime::currentDateTime(); //NOTE:Reservations are not DATE CHANGEABLE.

        QString realSubtotal;
        if (Settings::addTax())
            realSubtotal = KGlobal::locale()->formatMoney(subTotalSum-discMoney+pDiscounts, QString(), 2);
        else
            realSubtotal = KGlobal::locale()->formatMoney(subTotalSum-totalTax+discMoney+pDiscounts, QString(), 2); //FIXME: es +discMoney o -discMoney??
            qDebug()<<"\n********** Total Taxes:"<<totalTax<<" total Discount:"<<discMoney<<" Prod Discounts:"<<pDiscounts;
        
        ticket.number = currentTransaction;
        ticket.subTotal = realSubtotal; //This is the subtotal-taxes-discount
        ticket.total  = reservationPayment; qDebug()<<" *************** totalSum:"<<totalSum;
        ticket.change = changeGiven;
        ticket.paidwith = payWith;
        ticket.itemcount = cantidad;
        ticket.cardnum = cardNum;
        ticket.cardAuthNum = authnumber;
        ticket.paidWithCard = ui_mainview.checkCard->isChecked();
        ticket.clientDisc = clientInfo.discount;
        ticket.clientDiscMoney = discMoney;
        ticket.buyPoints = buyPoints;
        ticket.clientPoints = clientInfo.points;
        ticket.lines = ticketLines;
        ticket.clientid = clientInfo.id;
        ticket.hasSpecialOrders = false;
        ticket.completingSpecialOrder = false;
        ticket.totalTax = totalTax;
        ticket.terminal = QString::number(Settings::editTerminalNumber());

        //TODO: Add TERMS AND CONDITIONS OF RESERVATIONS to the ticket ??

        // Suspend Reservation (save balance, transaction, clear widgets...)
        suspendReservation();
        // Change transaction STATUS to tReserved.
        myDb->setTransactionStatus(rInfo.transaction_id, tReserved);

        //send print ticket data
        printTicket(ticket);
        printTicket(ticket);


    } else {
        //Cannot reserve empty product list!
        KNotification *notify = new KNotification("information", this);
        notify->setText(i18n("Cannot make a reservation, no products on the list. Special Orders are not considered."));
        QPixmap pixmap = DesktopIcon("dialog-information",32);
        notify->setPixmap(pixmap);
        notify->sendEvent();
    }
}

//NOTE: Here the store owner/admin needs to know that when lemon makes a product reservation the product qty in stock is decremented.
//      So, if the reserved product is not completed, the item is physically in the store but not in stock. It must be re-stocked, which
//      is done with a stockCorrection with a reason of "Reservation not completed or cancelled."
void lemonView::suspendReservation()
{
    qulonglong count = productsHash.count();
    if ( operationStarted && count>0 ) {
        qulonglong tmpId = currentTransaction;
        qDebug()<<"THE TRANSACTION HAS BEEN RESERVED. Id="<<tmpId;
        // save transaction and balance
        updateTransaction();
        updateBalance(false);
        // clear widgets
        startAgain();
        //inform the user
        KNotification *notify = new KNotification("information", this);
        notify->setText(i18n("The sale %1 has been sucessfully reserved.", tmpId));
        QPixmap pixmap = DesktopIcon("dialog-information",32);
        notify->setPixmap(pixmap);
        notify->sendEvent();
    }
}

void lemonView::resumeReservation()
{
    qDebug()<<"finishingReservation:"<<finishingReservation<<" startingReservation:"<<startingReservation;
    if (finishingReservation || startingReservation) return;

    Azahar *myDb = new Azahar;
    myDb->setDatabase(db);
    ReservationsDialog *dlg = new ReservationsDialog(this, drawer, loggedUserId);
    dlg->setDb(db);

    if (dlg->exec()) {
        // Until now, the transaction has the total of totalREAL - reservation.payment.
        // When doing the finishTransaction we need to update the total to the REAL TOTAL only
        // We can use a flag to indicate to do such thing on the finishCurrentTransaction() method.
        startingReservation = false;
        finishingReservation= true;
        
        //get data
        QList<ProductInfo>      pList = dlg->getProductsList();
        qulonglong trNumber           = dlg->getSelectedTransaction();
        qulonglong clientId           = dlg->getSelectedClient();
        reservationPayment            = dlg->getReservationPayment();
        reservationId                 = dlg->getSelectedReservation();
        //Check if there is a transaction, and suspend it before resume the reservation.
        suspendSale();
        currentTransaction = trNumber;
        
        emit signalUpdateTransactionInfo();
        clientInfo = myDb->getClientInfo(clientId);
        int idx = ui_mainview.comboClients->findText(clientInfo.name,Qt::MatchCaseSensitive);
        if (idx>-1) ui_mainview.comboClients->setCurrentIndex(idx);
        updateClientInfo();
        refreshTotalLabel();
        //HERE the availability does not matter, the item is Physically Reserved.
        //FIXME: Consider the total amount when reserved, because product price could be changed.
        foreach(ProductInfo info, pList) {
            QString qtyXcode = QString::number(info.qtyOnList) + "*" + QString::number(info.code);
            availabilityDoesNotMatters = true;
            insertItem(qtyXcode);
            availabilityDoesNotMatters = false;
        }
        doNotAddMoreItems = true;
        qDebug()<<"DoNotAddMoreItems = "<<doNotAddMoreItems;
        updateBalance(false);
        updateTransaction();

        // Next is a comment for the time the transaction is fihising
        // TODO PRINT TICKET:
        //    When printing ticket, the total is for the total value of the products (without the reservation payment).
        //    We need (for accountants) the total to be real on the transaction info.
        //    But, write a note on the ticket saying that a reservation payment of $X.XX was done.
    }
    delete myDb;
}

void lemonView::postponeReservation()
{
    //this is to save the transaction with the tReserved state, instead of cancelTransaction.
    Azahar *myDb = new Azahar;
    myDb->setDatabase(db);

    myDb->setTransactionReservationStatus(getCurrentTransaction());
    delete myDb;
}

#include "lemonview.moc"



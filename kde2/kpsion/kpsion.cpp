/*-*-c++-*-
 * $Id$
 *
 * This file is part of plptools.
 *
 *  Copyright (C) 1999  Philip Proudman <philip.proudman@btinternet.com>
 *  Copyright (C) 2000, 2001 Fritz Elfert <felfert@to.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "kpsion.h"
#include "wizards.h"

#include <kapp.h>
#include <klocale.h>
#include <kaction.h>
#include <kstdaction.h>
#include <kconfig.h>
#include <kiconview.h>
#include <kmessagebox.h>
#include <kfileitem.h>

#include <qwhatsthis.h>
#include <qtimer.h>
#include <qlayout.h>
#include <qiodevice.h>
#include <qheader.h>
#include <qdir.h>
#include <qmessagebox.h>

#include <ppsocket.h>
#include <rfsvfactory.h>
#include <rpcsfactory.h>
#include <bufferarray.h>

// internal use for developing offline without
// having a Psion connected.
// !!!!! set to 0 for production code !!!!!
#define OFFLINE 0

#define STID_CONNECTION 1

class KPsionCheckListItem::KPsionCheckListItemMetaData {
    friend KPsionCheckListItem;

private:
    KPsionCheckListItemMetaData();
    ~KPsionCheckListItemMetaData() { };

    bool parentIsKPsionCheckListItem;
    bool dontPropagate;
    int backupType;
    int size;
    time_t when;
    u_int32_t timeHi;
    u_int32_t timeLo;
    u_int32_t attr;
    QString name;
};

KPsionCheckListItem::KPsionCheckListItemMetaData::KPsionCheckListItemMetaData() {
    when = 0;
    size = 0;
    timeHi = 0;
    timeLo = 0;
    attr = 0;
    name = QString::null;
    backupType = KPsionBackupListView::UNKNOWN;
}

KPsionCheckListItem::~KPsionCheckListItem() {
    delete meta;
}

KPsionCheckListItem *KPsionCheckListItem::
firstChild() const {
    return (KPsionCheckListItem *)QListViewItem::firstChild();
}

KPsionCheckListItem *KPsionCheckListItem::
nextSibling() const {
    return (KPsionCheckListItem *)QListViewItem::nextSibling();
}

void KPsionCheckListItem::
init(bool myparent) {
    setSelectable(false);
    meta = new KPsionCheckListItemMetaData();
    meta->dontPropagate = false;
    meta->parentIsKPsionCheckListItem = myparent;
}

void KPsionCheckListItem::
setMetaData(int type, time_t when, QString name, int size,
	    u_int32_t tHi, u_int32_t tLo, u_int32_t attr) {
	meta->backupType = type;
	meta->when = when;
	meta->name = name;
	meta->size = size;
	meta->timeHi = tHi;
	meta->timeLo = tLo;
	meta->attr = attr;
}

void KPsionCheckListItem::
stateChange(bool state) {
    QCheckListItem::stateChange(state);

    if (meta->dontPropagate)
	return;
    if (meta->parentIsKPsionCheckListItem)
	((KPsionCheckListItem *)QListViewItem::parent())->propagateUp(state);
    else
	emit rootToggled();
    propagateDown(state);
}

void KPsionCheckListItem::
propagateDown(bool state) {
    setOn(state);
    KPsionCheckListItem *child = firstChild();
    while (child) {
	child->propagateDown(state);
	child = child->nextSibling();
    }
}

void KPsionCheckListItem::
propagateUp(bool state) {
    bool deactivateThis = false;

    KPsionCheckListItem *child = firstChild();
    while (child) {
	if ((child->isOn() != state) || (!child->isEnabled())) {
	    deactivateThis = true;
	    break;
	}
	child = child->nextSibling();
    }
    meta->dontPropagate = true;
    if (deactivateThis) {
	setOn(true);
	setEnabled(false);
    } else {
	setEnabled(true);
	setOn(state);
    }
    // Bug in QListView? It doesn't update, when
    // enabled/disabled without activating.
    // -> force it.
    listView()->repaintItem(this);
    meta->dontPropagate = false;
    if (meta->parentIsKPsionCheckListItem)
	((KPsionCheckListItem *)QListViewItem::parent())->propagateUp(state);
    else
	emit rootToggled();
}

QString KPsionCheckListItem::
key(int column, bool ascending) const {
    if (meta->when) {
	QString tmp;
	tmp.sprintf("%08d", meta->when);
	return tmp;
    }
    return text();
}

QString KPsionCheckListItem::
psionname() {
    if (meta->parentIsKPsionCheckListItem)
	return meta->name;
    else
	return QString::null;
}

QString KPsionCheckListItem::
unixname() {
    if (meta->parentIsKPsionCheckListItem)
	return KPsionMainWindow::psion2unix(meta->name);
    else
	return QString::null;
}

QString KPsionCheckListItem::
tarname() {
    if (meta->parentIsKPsionCheckListItem)
	return ((KPsionCheckListItem *)QListViewItem::parent())->tarname();
    else
	return meta->name;
}

int KPsionCheckListItem::
backupType() {
    if (meta->parentIsKPsionCheckListItem)
	return ((KPsionCheckListItem *)QListViewItem::parent())->backupType();
    else
	return meta->backupType;
}

time_t KPsionCheckListItem::
when() {
    if (meta->parentIsKPsionCheckListItem)
	return ((KPsionCheckListItem *)QListViewItem::parent())->when();
    else
	return meta->when;
}

PlpDirent KPsionCheckListItem::
plpdirent() {
    assert(meta->parentIsKPsionCheckListItem);
    return PlpDirent(meta->size, meta->attr, meta->timeHi, meta->timeLo,
		     meta->name);
}

KPsionBackupListView::KPsionBackupListView(QWidget *parent, const char *name)
    : KListView(parent, name) {

    toRestore.clear();
    uid = QString::null;
    KConfig *config = kapp->config();
    config->setGroup("Settings");
    backupDir = config->readEntry("BackupDir");
    addColumn(i18n("Available backups"));
    setRootIsDecorated(true);
}

KPsionCheckListItem *KPsionBackupListView::
firstChild() const {
    return (KPsionCheckListItem *)QListView::firstChild();
}

void KPsionBackupListView::
readBackups(QString uid) {
    QString bdir(backupDir);
    bdir += "/";
    bdir += uid;
    QDir d(bdir);
    kapp->processEvents();
    const QFileInfoList *fil =
	d.entryInfoList("*.tar.gz", QDir::Files|QDir::Readable, QDir::Name);
    QFileInfoListIterator it(*fil);
    QFileInfo *fi;
    while ((fi = it.current())) {
	kapp->processEvents();

	bool isValid = false;
	KTarGz tgz(fi->absFilePath());
	const KTarEntry *te;
	QString bTypeName;
	int bType;
	QDateTime date;

	tgz.open(IO_ReadOnly);
	te = tgz.directory()->entry("KPsionFullIndex");
	if (te && (!te->isDirectory())) {
	    date.setTime_t(te->date());
	    bTypeName = i18n("Full");
	    bType = FULL;
	    isValid = true;
	} else {
	    te = tgz.directory()->entry("KPsionIncrementalIndex");
	    if (te && (!te->isDirectory())) {
		date.setTime_t(te->date());
		bTypeName = i18n("Incremental");
		bType = INCREMENTAL;
		isValid = true;
	    }
	}

	if (isValid) {
	    QString n =	i18n("%1 backup, created at %2").arg(bTypeName).arg(KGlobal::locale()->formatDateTime(date, false));
	    QByteArray a = ((KTarFile *)te)->data();
	    QTextIStream indexData(a);

	    indexData.readLine();
	    indexData.flags(QTextStream::hex);
	    indexData.fill('0');
	    indexData.width(8);

	    KPsionCheckListItem *i =
		new KPsionCheckListItem(this, n,
					KPsionCheckListItem::CheckBox);
	    i->setMetaData(bType, te->date(), tgz.fileName(), 0, 0, 0, 0);
	    i->setPixmap(0, KGlobal::iconLoader()->loadIcon("mime_empty",
							 KIcon::Small));
	    connect(i, SIGNAL(rootToggled()), this, SLOT(slotRootToggled()));

	    QStringList files = tgz.directory()->entries();
	    for (QStringList::Iterator f = files.begin();
		 f != files.end(); f++)
		if ((*f != "KPsionFullIndex") &&
		    (*f != "KPsionIncrementalIndex"))
		    listTree(i, tgz.directory()->entry(*f), indexData, 0);
	}
	tgz.close();
	++it;
    }
    header()->setClickEnabled(false);
    header()->setResizeEnabled(false);
    header()->setMovingEnabled(false);
    setMinimumSize(columnWidth(0) + 4, height());
    QWhatsThis::add(this, i18n(
			"<qt>Here, you can select the available backups."
			" Select the items you want to restore, the click"
			" on <b>Start</b> to start restoring these items."
			"</qt>"));
}

void KPsionBackupListView::
listTree(KPsionCheckListItem *cli, const KTarEntry *te, QTextIStream &idx,
	 int level) {
    KPsionCheckListItem *i =
	new KPsionCheckListItem(cli, te->name(),
				KPsionCheckListItem::CheckBox);
    kapp->processEvents();
    if (te->isDirectory()) {
	if (level)
	    i->setPixmap(0, KGlobal::iconLoader()->loadIcon("folder",
							    KIcon::Small));
	else
	    i->setPixmap(0, KGlobal::iconLoader()->loadIcon("hdd_unmount",
							    KIcon::Small));
	i->setMetaData(0, 0, QString::null, 0, 0, 0, 0);
	KTarDirectory *td = (KTarDirectory *)te;
	QStringList files = td->entries();
	for (QStringList::Iterator f = files.begin(); f != files.end(); f++)
	    listTree(i, td->entry(*f), idx, level + 1);
    } else {
	uint32_t timeHi;
	uint32_t timeLo;
	uint32_t size;
	uint32_t attr;
	QString  name;

	i->setPixmap(0, KGlobal::iconLoader()->loadIcon("mime_empty",
							KIcon::Small));
	idx >> timeHi >> timeLo >> size >> attr >> name;
	i->setMetaData(0, 0, name, size, timeHi, timeLo, attr);
    }
}

void KPsionBackupListView::
slotRootToggled() {
    bool anyOn = false;
    KPsionCheckListItem *i = firstChild();
    while (i != 0L) {
	if (i->isOn()) {
	    anyOn = true;
	    break;
	}
	i = i->nextSibling();
    }
    emit itemsEnabled(anyOn);
}

QStringList KPsionBackupListView::
getSelectedTars() {
    QStringList l;

    KPsionCheckListItem *i = firstChild();
    while (i != 0L) {
	if (i->isOn())
	    l += i->tarname();
	i = i->nextSibling();
    }
    return l;
}

bool KPsionBackupListView::
autoSelect(QString drive) {
    KPsionCheckListItem *latest = NULL;
    time_t stamp = 0;
    
    drive += ":";
    // Find latest full backup for given drive
    KPsionCheckListItem *i = firstChild();
    while (i != 0L) {
	if ((i->backupType() == FULL) && (i->when() > stamp)) {
	    KPsionCheckListItem *c = i->firstChild();
	    while (c != 0L) {
		if (c->text() == drive) {
		    latest = c;
		    stamp = i->when();
		    break;
		}
		c = c->nextSibling();
	    }
	}
	i = i->nextSibling();
    }
    // Now find all incremental backups for given drive which are newer than
    // selected backup
    if (latest != 0) {
	latest->setOn(true);
	i = firstChild();
	while (i != 0L) {
	    if ((i->backupType() == INCREMENTAL) && (i->when() >= stamp)) {
		KPsionCheckListItem *c = i->firstChild();
		while (c != 0L) {
		    if (c->text() == drive)
			c->setOn(true);
		    c = c->nextSibling();
		}
	    }
	    i = i->nextSibling();
	}
    }
    return (latest != 0L);
}

void KPsionBackupListView::
collectEntries(KPsionCheckListItem *i) {
    while (i != 0L) {
	KPsionCheckListItem *c = i->firstChild();
	if (c == 0L) {
	    if (i->isOn())
		toRestore.push_back(i->plpdirent());
	} else
	    collectEntries(c);
	i = i->nextSibling();
    }
}

PlpDir &KPsionBackupListView::
getRestoreList(QString tarname) {
    toRestore.clear();
    KPsionCheckListItem *i = firstChild();
    while (i != 0L) {
	if ((i->tarname() == tarname) && (i->isOn())) {
	    collectEntries(i->firstChild());
	    break;
	}
	i = i->nextSibling();
    }
    return toRestore;
}

KPsionMainWindow::KPsionMainWindow()
    : KMainWindow() {
    setupActions();

    statusBar()->insertItem(i18n("Idle"), STID_CONNECTION, 1);
    statusBar()->setItemAlignment(STID_CONNECTION,
				  QLabel::AlignLeft|QLabel::AlignVCenter);

    progress = new KPsionStatusBarProgress(statusBar(), "progressBar");
    statusBar()->addWidget(progress, 10);

    connect(progress, SIGNAL(pressed()), this, SLOT(slotProgressBarPressed()));
    connect(this, SIGNAL(setProgress(int)), progress, SLOT(setValue(int)));
    connect(this, SIGNAL(setProgress(int, int)), progress,
			 SLOT(setValue(int, int)));
    connect(this, SIGNAL(setProgressText(const QString &)), progress,
			 SLOT(setText(const QString &)));
    connect(this, SIGNAL(enableProgressText(bool)), progress,
			 SLOT(setTextEnabled(bool)));

    backupRunning = false;
    restoreRunning = false;
    formatRunning = false;

    view = new KIconView(this, "iconview");
    view->setSelectionMode(KIconView::Multi);
    view->setResizeMode(KIconView::Adjust);
    view->setItemsMovable(false);
    connect(view, SIGNAL(clicked(QIconViewItem *)),
	    SLOT(iconClicked(QIconViewItem *)));
    connect(view, SIGNAL(onItem(QIconViewItem *)),
	    SLOT(iconOver(QIconViewItem *)));
    KConfig *config = kapp->config();
    config->setGroup("Psion");
    QStringList uids = config->readListEntry("MachineUIDs");
    for (QStringList::Iterator it = uids.begin(); it != uids.end(); it++) {
	QString tmp = QString::fromLatin1("Name_%1").arg(*it);
	machines.insert(*it, config->readEntry(tmp));
    }
    config->setGroup("Settings");
    backupDir = config->readEntry("BackupDir");
    config->setGroup("Connection");
    reconnectTime = config->readNumEntry("Retry");
    ncpdDevice = config->readEntry("Device", i18n("off"));
    ncpdSpeed = config->readNumEntry("Speed", 115200);

    QWhatsThis::add(view, i18n(
			"<qt>Here, you see your Psion's drives.<br/>"
			"Every drive is represented by an Icon. If you "
			"click on it, it gets selected for the next "
			"operation. E.g.: backup, restore or format.<br/>"
			"To unselect it, simply click on it again.<br/>"
			"Select as many drives a you want, then choose "
			"an operation.</qt>"));
    setCentralWidget(view);

    rfsvSocket = 0L;
    rpcsSocket = 0L;
    plpRfsv = 0L;
    plpRpcs = 0L;

    firstTry = true;
    connected = false;
    shuttingDown = false;

    args = KCmdLineArgs::parsedArgs();
    if (args->isSet("autobackup")) {
	firstTry = false;
	reconnectTime = 0;
    }
    tryConnect();
}

KPsionMainWindow::~KPsionMainWindow() {
    shuttingDown = true;
    if (plpRfsv)
	delete plpRfsv;
    if (plpRpcs)
	delete plpRpcs;
    if (rfsvSocket)
	delete rfsvSocket;
    if (rfsvSocket)
	delete rpcsSocket;
}

QString KPsionMainWindow::
unix2psion(const char * const path) {
    QString tmp = path;
    tmp.replace(QRegExp("/"), "\\");
    tmp.replace(QRegExp("%2f"), "/");
    tmp.replace(QRegExp("%25"), "%");
    return tmp;
}

QString KPsionMainWindow::
psion2unix(const char * const path) {
    QString tmp = path;
    tmp.replace(QRegExp("%"), "%25");
    tmp.replace(QRegExp("/"), "%2f");
    tmp.replace(QRegExp("\\"), "/");
    return tmp;
}

void KPsionMainWindow::
setupActions() {

    KStdAction::quit(this, SLOT(close()), actionCollection());
    KStdAction::showToolbar(this, SLOT(slotToggleToolbar()),
			    actionCollection());
    KStdAction::showStatusbar(this, SLOT(slotToggleStatusbar()),
			      actionCollection());
    KStdAction::saveOptions(this, SLOT(slotSaveOptions()),
			    actionCollection());
    KStdAction::preferences(this, SLOT(slotPreferences()),
			    actionCollection());
    new KAction(i18n("Start &Format"), 0L, 0, this,
		SLOT(slotStartFormat()), actionCollection(), "format");
    new KAction(i18n("Start Full &Backup"), "psion_backup", 0, this,
		SLOT(slotStartFullBackup()), actionCollection(),
		"fullbackup");
    new KAction(i18n("Start &Incremental Backup"), "psion_backup", 0, this,
		SLOT(slotStartIncBackup()), actionCollection(), "incbackup");
    new KAction(i18n("Start &Restore"), "psion_restore", 0, this,
		SLOT(slotStartRestore()), actionCollection(), "restore");
    createGUI();

    actionCollection()->action("fullbackup")->setEnabled(false);
    actionCollection()->action("incbackup")->setEnabled(false);
#if OFFLINE
    actionCollection()->action("restore")->setEnabled(true);
#else
    actionCollection()->action("restore")->setEnabled(false);
#endif
    actionCollection()->action("format")->setEnabled(false);

    actionCollection()->action("fullbackup")->
	setToolTip(i18n("Full backup of selected drive(s)"));
    actionCollection()->action("incbackup")->
	setToolTip(i18n("Incremental backup of selected drive(s)"));
    actionCollection()->action("restore")->
	setToolTip(i18n("Restore selected drive(s)"));
    actionCollection()->action("format")->
	setToolTip(i18n("Format selected drive(s)"));
}

void KPsionMainWindow::
iconOver(QIconViewItem *i) {
    lastSelected = i->isSelected();
}

void KPsionMainWindow::
switchActions() {
    QIconViewItem *i;
    bool rwSelected = false;
    bool anySelected = false;

    if (backupRunning | restoreRunning | formatRunning)
	view->setEnabled(false);
    else {
	for (i = view->firstItem(); i; i = i->nextItem()) {
	    if (i->isSelected()) {
		anySelected = true;
		if (i->key() != "Z") {
		    rwSelected = true;
		    break;
		}
	    }
	}
	view->setEnabled(true);
    }
#if OFFLINE
    actionCollection()->action("restore")->setEnabled(true);
#else
    actionCollection()->action("restore")->setEnabled(rwSelected);
#endif
    actionCollection()->action("format")->setEnabled(rwSelected);
    actionCollection()->action("fullbackup")->setEnabled(anySelected);
    actionCollection()->action("incbackup")->setEnabled(anySelected);
}

void KPsionMainWindow::
iconClicked(QIconViewItem *i) {
    if (i == 0L)
	return;
    lastSelected = !lastSelected;
    i->setSelected(lastSelected);
    switchActions();
}

void KPsionMainWindow::
insertDrive(char letter, const char * const name) {
    QString tmp;

    if (name && strlen(name))
	tmp = QString::fromLatin1("%1 (%2:)").arg(name).arg(letter);
    else
	tmp = QString::fromLatin1("%1:").arg(letter);
    drives.insert(letter,tmp);
    QIconViewItem *it =
	new QIconViewItem(view, tmp,
			  KFileItem(KURL(), "inode/x-psion-drive", 0).pixmap(0));
    tmp = QString::fromLatin1("%1").arg(letter);
    it->setKey(tmp);
    it->setDropEnabled(false);
    it->setDragEnabled(false);
    it->setRenameEnabled(false);
}

void KPsionMainWindow::
queryPsion() {
    u_int32_t devbits;
    Enum <rfsv::errs> res;

    statusBar()->changeItem(i18n("Retrieving machine info ..."),
			    STID_CONNECTION);
#if OFFLINE
    machineUID = 0x1000118a0c428fa3ULL;
    S5mx = true;
    insertDrive('C', "Intern");
    insertDrive('D', "Flash");
    insertDrive('Z', "RomDrive");
#else
    rpcs::machineInfo mi;
    if ((res = plpRpcs->getMachineInfo(mi)) != rfsv::E_PSI_GEN_NONE) {
	QString msg = i18n("Could not get Psion machine info");
	statusBar()->changeItem(msg, STID_CONNECTION);
	KMessageBox::error(this, msg);
	return;
    }
    machineUID = mi.machineUID;
    S5mx = (strcmp(mi.machineName, "SERIES5mx") == 0);
#endif

    QString uid = getMachineUID();
    bool machineFound = false;
    KConfig *config = kapp->config();
    config->setGroup("Psion");
    machineName = i18n("an unknown machine");
    psionMap::Iterator it;
    for (it = machines.begin(); it != machines.end(); it++) {
	if (uid == it.key()) {
	    machineName = it.data();
	    QString tmp =
		QString::fromLatin1("BackupDrives_%1").arg(it.key());
	    backupDrives = config->readListEntry(tmp);
	    machineFound = true;
	}
    }
#if (!(OFFLINE))
    drives.clear();
    statusBar()->changeItem(i18n("Retrieving drive list ..."),
			    STID_CONNECTION);
    if ((res = plpRfsv->devlist(devbits)) != rfsv::E_PSI_GEN_NONE) {
	QString msg = i18n("Could not get list of drives");
	statusBar()->changeItem(msg, STID_CONNECTION);
	KMessageBox::error(this, msg);
	return;
    }
    for (int i = 0; i < 26; i++) {
	if ((devbits & 1) != 0) {
	    PlpDrive drive;
	    if (plpRfsv->devinfo(i, drive) == rfsv::E_PSI_GEN_NONE)
		insertDrive('A' + i, drive.getName().c_str());
	}
	devbits >>= 1;
    }
#endif
    if (!machineFound) {
	if (args->isSet("autobackup")) {
	    connected = false;
	    if (plpRfsv)
		delete plpRfsv;
	    if (plpRpcs)
		delete plpRpcs;
	    if (rfsvSocket)
		delete rfsvSocket;
	    if (rfsvSocket)
		delete rpcsSocket;
	    return;
	}
	NewPsionWizard *wiz = new NewPsionWizard(this, "newpsionwiz");
	wiz->exec();
    }
    statusBar()->changeItem(i18n("Connected to %1").arg(machineName),
			    STID_CONNECTION);
}

QString KPsionMainWindow::
getMachineUID() {
    // ??! None of QString's formatting methods knows about long long.
    char tmp[20];
    sprintf(tmp, "%16llx", machineUID);
    return QString(tmp);
}

bool KPsionMainWindow::
queryClose() {
    QString msg = 0L;

    if (backupRunning)
	msg = i18n("A backup is running.\nDo you really want to quit?");
    if (restoreRunning)
	msg = i18n("A restore is running.\nDo you really want to quit?");
    if (formatRunning)
	msg = i18n("A format is running.\nDo you really want to quit?");

    if ((!msg.isNull()) &&
	(KMessageBox::warningYesNo(this, msg) == KMessageBox::No))
	return false;
    return true;
}

void KPsionMainWindow::
tryConnect() {
#if (!(OFFLINE))
    if (shuttingDown || connected)
	return;
    bool showMB = firstTry;
    firstTry = false;

    if (plpRfsv)
	delete plpRfsv;
    if (plpRpcs)
	delete plpRpcs;
    if (rfsvSocket)
	delete rfsvSocket;
    if (rfsvSocket)
	delete rpcsSocket;

    rfsvSocket = new ppsocket();
    statusBar()->changeItem(i18n("Connecting ..."), STID_CONNECTION);
    if (!rfsvSocket->connect(NULL, 7501)) {
	statusMsg = i18n("RFSV could not connect to ncpd at %1:%2. ").arg("localhost").arg(7501);
	if (reconnectTime) {
	    nextTry = reconnectTime;
	    statusMsg += i18n(" (Retry in %1 seconds.)");
	    QTimer::singleShot(1000, this, SLOT(slotUpdateTimer()));

	    statusBar()->changeItem(statusMsg.arg(reconnectTime),
				    STID_CONNECTION);
	    if (showMB)
		KMessageBox::error(this, statusMsg.arg(reconnectTime));
	} else {
	    statusBar()->changeItem(statusMsg, STID_CONNECTION);
	    if (showMB)
		KMessageBox::error(this, statusMsg);
	}
	return;
    }
    rfsvfactory factory(rfsvSocket);
    plpRfsv = factory.create(false);
    if (plpRfsv == 0L) {
	statusMsg = i18n("RFSV could not establish link: %1.").arg(KGlobal::locale()->translate(factory.getError()));
	delete rfsvSocket;
	rfsvSocket = 0L;
	if (reconnectTime) {
	    nextTry = reconnectTime;
	    statusMsg += i18n(" (Retry in %1 seconds.)");
	    QTimer::singleShot(1000, this, SLOT(slotUpdateTimer()));
	    statusBar()->changeItem(statusMsg.arg(reconnectTime),
				    STID_CONNECTION);
	    if (showMB)
		KMessageBox::error(this, statusMsg.arg(reconnectTime));
	} else {
	    statusBar()->changeItem(statusMsg, STID_CONNECTION);
	    if (showMB)
		KMessageBox::error(this, statusMsg);
	}
	return;
    }

    rpcsSocket = new ppsocket();
    if (!rpcsSocket->connect(NULL, 7501)) {
	statusMsg = i18n("RPCS could not connect to ncpd at %1:%2.").arg("localhost").arg(7501);
	delete plpRfsv;
	plpRfsv = 0L;
	delete rfsvSocket;
	rfsvSocket = 0L;
	if (reconnectTime) {
	    nextTry = reconnectTime;
	    statusMsg += i18n(" (Retry in %1 seconds.)");
	    QTimer::singleShot(1000, this, SLOT(slotUpdateTimer()));
	    statusBar()->changeItem(statusMsg.arg(reconnectTime),
				    STID_CONNECTION);
	    if (showMB)
		KMessageBox::error(this, statusMsg.arg(reconnectTime));
	} else {
	    statusBar()->changeItem(statusMsg, STID_CONNECTION);
	    if (showMB)
		KMessageBox::error(this, statusMsg);
	}
	return;
    }
    rpcsfactory factory2(rpcsSocket);
    plpRpcs = factory2.create(false);
    if (plpRpcs == 0L) {
	statusMsg = i18n("RPCS could not establish link: %1.").arg(KGlobal::locale()->translate(factory2.getError()));
	delete plpRfsv;
	plpRfsv = 0L;
	delete rfsvSocket;
	rfsvSocket = 0L;
	delete rpcsSocket;
	rpcsSocket = 0L;
	if (reconnectTime) {
	    nextTry = reconnectTime;
	    statusMsg += i18n(" (Retry in %1 seconds.)");
	    QTimer::singleShot(1000, this, SLOT(slotUpdateTimer()));
	    statusBar()->changeItem(statusMsg.arg(reconnectTime),
				    STID_CONNECTION);
	    if (showMB)
		KMessageBox::error(this, statusMsg.arg(reconnectTime));
	} else {
	    statusBar()->changeItem(statusMsg, STID_CONNECTION);
	    if (showMB)
		KMessageBox::error(this, statusMsg);
	}
	return;
    }
#endif
    connected = true;
    queryPsion();
}

void KPsionMainWindow::
slotUpdateTimer() {
    nextTry--;
    if (nextTry <= 0)
	tryConnect();
    else {
	statusBar()->changeItem(statusMsg.arg(nextTry), STID_CONNECTION);
	QTimer::singleShot(1000, this, SLOT(slotUpdateTimer()));
    }
}

void KPsionMainWindow::
slotProgressBarPressed() {
}

void KPsionMainWindow::
slotStartFullBackup() {
    fullBackup = true;
    doBackup();
}

void KPsionMainWindow::
slotStartIncBackup() {
    fullBackup = false;
    doBackup();
}

const KTarEntry *KPsionMainWindow::
findTarEntry(const KTarEntry *te, QString path, QString rpath) {
    const KTarEntry *fte = NULL;
    if (te->isDirectory() && (path.left(rpath.length()) == rpath)) {
	KTarDirectory *td = (KTarDirectory *)te;
	QStringList files = td->entries();
	for (QStringList::Iterator f = files.begin(); f != files.end(); f++) {
	    QString tmp = rpath;
	    if (tmp.length())
		tmp += "/";
	    tmp += *f;
	    fte = findTarEntry(td->entry(*f), path, tmp);
	    if (fte != 0L)
		break;
	}
    } else {
	if (path == rpath)
	    fte = te;
    }
    return fte;
}

void KPsionMainWindow::
doBackup() {
    backupRunning = true;
    switchActions();
    toBackup.clear();

    // Collect list of files to backup
    backupSize = 0;
    backupCount = 0;
    progressTotal = 0;
    emit enableProgressText(true);
    emit setProgress(0);
    for (QIconViewItem *i = view->firstItem(); i; i = i->nextItem()) {
	if (i->isSelected()) {
	    QString drv = i->key();
	    drv += ":";
	    int drvNum = *(drv.data()) - 'A';
	    PlpDrive drive;
	    if (plpRfsv->devinfo(drvNum, drive) != rfsv::E_PSI_GEN_NONE) {
		statusBar()->changeItem(i18n("Connected to %1").arg(machineName),
					STID_CONNECTION);
		emit enableProgressText(false);
		emit setProgress(0);
		KMessageBox::error(this, i18n("Could not retrieve drive details for drive %1").arg(drv));
		backupRunning = false;
		switchActions();
		return;
	    }
	    emit setProgressText(i18n("Scanning drive %1").arg(drv));

	    progressLocal = drive.getSize() - drive.getSpace();
	    progressLocalCount = 0;
	    progressLocalPercent = -1;
	    progress->setValue(0);
	    collectFiles(drv);
	}
    }
    emit setProgressText(i18n("%1 files need backup").arg(backupSize));
    if (backupCount == 0) {
	emit enableProgressText(false);
	emit setProgress(0);
	statusBar()->changeItem(i18n("Connected to %1").arg(machineName),
				STID_CONNECTION);
	KMessageBox::information(this, i18n("No files need backup"));
	backupRunning = false;
	switchActions();
	return;
    }

    // statusBar()->message(i18n("Backup"));
    progressCount = 0;
    progressTotal = backupSize;
    progressPercent = -1;

    // Create tgz with index file.
    QString archiveName = backupDir;
    if (archiveName.right(1) != "/")
	archiveName += "/";
    archiveName += getMachineUID();
    QDir archiveDir(archiveName);
    if (!archiveDir.exists())
	if (!archiveDir.mkdir(archiveName)) {
	    emit enableProgressText(false);
	    emit setProgress(0);
	    statusBar()->changeItem(i18n("Connected to %1").arg(machineName),
				    STID_CONNECTION);
	    KMessageBox::error(this, i18n("Could not create backup folder %1").arg(archiveName));
	    // statusBar()->clear();
	    backupRunning = false;
	    switchActions();
	    return;
	}

    archiveName += (fullBackup) ? "/F-" : "/I-";
    time_t now = time(0);
    char tstr[30];
    strftime(tstr, sizeof(tstr), "%Y-%m-%d-%H-%M-%S.tar.gz",
	     localtime(&now));
    archiveName += tstr;
    backupTgz = new KTarGz(archiveName);
    backupTgz->open(IO_WriteOnly);
    createIndex();

    // Kill all running applications on the Psion
    // and save their state.
    killSave();

    bool badBackup = false;
    Enum<rfsv::errs> res;
    // Now the real backup
    progressTotalText = i18n("Backup %1% done");
    for (int i = 0; i < toBackup.size(); i++) {
	PlpDirent e = toBackup[i];
	const char *fn = e.getName();
	QString unixname = psion2unix(fn);
	QByteArray ba;
	QDataStream os(ba, IO_WriteOnly);

	emit setProgressText(QString("%1").arg(fn));
	progressLocal = e.getSize();
	progressLocalCount = 0;
	progressLocalPercent = -1;
	emit setProgress(0);

	u_int32_t handle;

	kapp->processEvents();
	res = plpRfsv->fopen(plpRfsv->opMode(rfsv::PSI_O_RDONLY), fn,
			     handle);
	if (res != rfsv::E_PSI_GEN_NONE) {
	    if (KMessageBox::warningYesNo(this, i18n("<QT>Could not open<BR/><B>%1</B></QT>").arg(fn)) == KMessageBox::No) {
		badBackup = true;
		break;
	    } else {
		e.setName("!");
		continue;
	    }
	}
	unsigned char *buff = new unsigned char[RFSV_SENDLEN];
	u_int32_t len;
	do {
	    if ((res = plpRfsv->fread(handle, buff, RFSV_SENDLEN, len)) ==
		rfsv::E_PSI_GEN_NONE) {
		os.writeRawBytes((char *)buff, len);
		updateProgress(len);
	    }
	} while ((len > 0) && (res == rfsv::E_PSI_GEN_NONE));
	delete[]buff;
	plpRfsv->fclose(handle);
	if (res != rfsv::E_PSI_GEN_NONE) {
	    if (KMessageBox::warningYesNo(this, i18n("<QT>Could not read<BR/><B>%1</B></QT>").arg(fn)) == KMessageBox::No) {
		badBackup = true;
		break;
	    } else {
		e.setName("!");
		continue;
	    }
	}
	backupTgz->writeFile(unixname, "root", "root", ba.size(), ba.data());
    }

    if (!badBackup) {
	// Reset archive attributes of all backuped files.
	emit setProgressText(i18n("Resetting archive attributes"));
	progressLocal = backupSize;
	progressLocalCount = 0;
	progressLocalPercent = -1;
	emit setProgress(0);
	for (int i = 0; i < toBackup.size(); i++) {
	    PlpDirent e = toBackup[i];
	    const char *fn = e.getName();
	    if ((e.getAttr() & rfsv::PSI_A_ARCHIVE) &&
		(strcmp(fn, "!") != 0)) {
		kapp->processEvents();
		res = plpRfsv->fsetattr(fn, 0, rfsv::PSI_A_ARCHIVE);
		if (res != rfsv::E_PSI_GEN_NONE) {
		    if (KMessageBox::warningYesNo(this, i18n("<QT>Could not set attributes of<BR/><B>%1</B></QT>").arg(fn)) == KMessageBox::No) {
			break;
		    }
		}
	    }
	    updateProgress(e.getSize());
	}
    }
    // Restart previously running applications on the Psion
    // from saved state info.
    runRestore();

    backupTgz->close();
    delete backupTgz;
    if (badBackup)
	unlink(archiveName.data());
    backupRunning = false;
    switchActions();
    statusBar()->changeItem(i18n("Connected to %1").arg(machineName),
			    STID_CONNECTION);
    emit enableProgressText(false);
    emit setProgress(0);
    statusBar()->message(i18n("Backup done"), 2000);
}

KPsionRestoreDialog::KPsionRestoreDialog(QWidget *parent, QString uid)
    : KDialogBase(parent, "restoreDialog", true, i18n("Restore"),
		  KDialogBase::Cancel | KDialogBase::Ok,
		  KDialogBase::Ok, true) {

    setButtonOKText(i18n("Start"));
    enableButtonOK(false);
    setButtonWhatsThis(KDialogBase::Ok,
		       i18n("Select items in the list of"
			    " available backups, then click"
			    " here to start restore of these"
			    " items."));

    QWidget *w = new QWidget(this);
    setMainWidget(w);
    QGridLayout *gl = new QGridLayout(w, 1, 1, KDialog::marginHint(),
				      KDialog::marginHint());
    backupView = new KPsionBackupListView(w, "restoreSelector");
    gl->addWidget(backupView, 0, 0);
    backupView->readBackups(uid);
    connect(backupView, SIGNAL(itemsEnabled(bool)), this,
	    SLOT(slotBackupsSelected(bool)));
}

void KPsionRestoreDialog::
slotBackupsSelected(bool any) {
    enableButtonOK(any);
}

QStringList KPsionRestoreDialog::
getSelectedTars() {
    return backupView->getSelectedTars();
}

bool KPsionRestoreDialog::
autoSelect(QString drive) {
    return backupView->autoSelect(drive);
}

PlpDir &KPsionRestoreDialog::
getRestoreList(QString tarname) {
    return backupView->getRestoreList(tarname);
}

bool KPsionMainWindow::
askOverwrite(PlpDirent e) {
    if (overWriteAll)
	return true;
    const char *fn = e.getName();
    if (overWriteList.contains(QString(fn)))
	return true;
    PlpDirent old;
    Enum<rfsv::errs> res = plpRfsv->fgeteattr(fn, old);
    if (res != rfsv::E_PSI_GEN_NONE) {
	KMessageBox::error(this, i18n(
	    "<QT>Could not get attributes of<BR/>"
	    "<B>%1</B><BR/>Reason: %2</QT>").arg(fn).arg(KGlobal::locale()->translate(res)));
	return false;
    }

    // Don't ask if size and attribs are same
    if ((old.getSize() == e.getSize()) &&
	((old.getAttr() & ~rfsv::PSI_A_ARCHIVE) ==
	 (e.getAttr() & ~rfsv::PSI_A_ARCHIVE)))
	return true;

    QDateTime odate;
    QDateTime ndate;

    odate.setTime_t(old.getPsiTime().getTime());
    ndate.setTime_t(e.getPsiTime().getTime());

    // Dates
    QString od = KGlobal::locale()->formatDateTime(odate, false);
    QString nd = KGlobal::locale()->formatDateTime(ndate, false);

    // Sizes
    QString os = QString("%1 (%2)").arg(KIO::convertSize(old.getSize())).arg(KGlobal::locale()->formatNumber(old.getSize(), 0));
    QString ns = QString("%1 (%2)").arg(KIO::convertSize(e.getSize())).arg(KGlobal::locale()->formatNumber(e.getSize(), 0));

    // Attributes
    QString oa(plpRfsv->attr2String(old.getAttr()).c_str());
    QString na(plpRfsv->attr2String(e.getAttr()).c_str());

    KDialogBase dialog(i18n("Overwrite"), KDialogBase::Yes | KDialogBase::No |
		       KDialogBase::Cancel, KDialogBase::No,
		       KDialogBase::Cancel, this, "overwriteDialog", true, true,
		       QString::null, QString::null, i18n("Overwrite &All"));
    
    QWidget *contents = new QWidget(&dialog);
    QHBoxLayout * lay = new QHBoxLayout(contents);
    lay->setSpacing(KDialog::spacingHint()*2);
    lay->setMargin(KDialog::marginHint()*2);
    
    lay->addStretch(1);
    QLabel *label1 = new QLabel(contents);
    label1->setPixmap(QMessageBox::standardIcon(QMessageBox::Warning,
						kapp->style().guiStyle()));
    lay->add(label1);
    lay->add(new QLabel(i18n(
	"<QT>The file <B>%1</B> exists already on the Psion with "
	"different size and/or attributes."
	"<P><B>On the Psion:</B><BR/>"
	"  Size: %2<BR/>"
	"  Date: %3<BR/>"
	"  Attributes: %4</P>"
	"<P><B>In backup:</B><BR/>"
	"  Size: %5<BR/>"
	"  Date: %6<BR/>"
	"  Attributes: %7</P>"
	"Do you want to overwrite it?</QT>").arg(fn).arg(os).arg(od).arg(oa).arg(ns).arg(nd).arg(na), contents));
    lay->addStretch(1);
 
    dialog.setMainWidget(contents);
    dialog.enableButtonSeparator(false);
 
    int result = dialog.exec();
    switch (result) {
	case KDialogBase::Yes:
	    return true;
	case KDialogBase::No:
	    return false;
	case KDialogBase::Cancel:
	    overWriteAll = true;
	    return true;
	default: // Huh?
	    break;
    }
 
    return false; // Default
}

void KPsionMainWindow::
slotStartRestore() {
    restoreRunning = true;
    switchActions();

    kapp->setOverrideCursor(Qt::waitCursor);
    statusBar()->changeItem(i18n("Reading backups ..."), STID_CONNECTION);
    update();
    kapp->processEvents();
    KPsionRestoreDialog restoreDialog(this, getMachineUID());
    kapp->restoreOverrideCursor();
    statusBar()->changeItem(i18n("Selecting backups ..."), STID_CONNECTION);

    for (QIconViewItem *i = view->firstItem(); i; i = i->nextItem()) {
	if (i->isSelected() && (i->key() != "Z"))
	    restoreDialog.autoSelect(i->key());
    }

    overWriteList.clear();
    overWriteAll = false;

    if (restoreDialog.exec() == KDialogBase::Accepted) {
	QStringList tars = restoreDialog.getSelectedTars();
	QStringList::Iterator t;

	backupSize = 0;
	backupCount = 0;
	for (t = tars.begin(); t != tars.end(); t++) {
	    PlpDir toRestore = restoreDialog.getRestoreList(*t);
	    for (int r = 0; r < toRestore.size(); r++) {
		PlpDirent e = toRestore[r];
		backupSize += e.getSize();
		backupCount++;
	    }
	}
	if (backupCount == 0) {
	    restoreRunning = false;
	    switchActions();
	    statusBar()->changeItem(i18n("Connected to %1").arg(machineName),
				    STID_CONNECTION);
	    return;
	}

	progressTotalText = i18n("Restore %1% done");
	progressTotal = backupSize;
	progressCount = 0;
	progressPercent = -1;
	emit setProgressText("");
	emit enableProgressText(true);
	emit setProgress(0);

	// Kill all running applications on the Psion
	// and save their state.
	killSave();

	for (t = tars.begin(); t != tars.end(); t++) {
	    PlpDir toRestore = restoreDialog.getRestoreList(*t);
	    if (toRestore.size() > 0) {
		KTarGz tgz(*t);
		const KTarEntry *te;
		QString pDir("");

		tgz.open(IO_ReadOnly);
		for (int r = 0; r < toRestore.size(); r++) {
		    PlpDirent e = toRestore[r];
		    PlpDirent olde;

		    const char *fn = e.getName();
		    QString unixname = psion2unix(fn);
		    Enum<rfsv::errs> res;

		    progressLocal = e.getSize();
		    progressLocalCount = 0;
		    progressLocalPercent = -1;
		    emit setProgressText(QString("%1").arg(fn));
		    emit setProgress(0);

		    te = findTarEntry(tgz.directory(), unixname);
		    if (te != 0L) {
			u_int32_t handle;
			QString cpDir(fn);
			QByteArray ba = ((KTarFile *)te)->data();
			int bslash = cpDir.findRev('\\');
			if (bslash > 0)
			    cpDir = cpDir.left(bslash);
			if (pDir != cpDir) {
			    pDir = cpDir;
			    res = plpRfsv->mkdir(pDir);
			    if ((res != rfsv::E_PSI_GEN_NONE) &&
				(res != rfsv::E_PSI_FILE_EXIST)) {
				KMessageBox::error(this, i18n(
				    "<QT>Could not create directory<BR/>"
				    "<B>%1</B><BR/>Reason: %2</QT>").arg(pDir).arg(KGlobal::locale()->translate(res)));
				continue;
			    }
			}
			res = plpRfsv->fcreatefile(
			    plpRfsv->opMode(rfsv::PSI_O_RDWR), fn, handle);
			if (res == rfsv::E_PSI_FILE_EXIST) {
			    if (!askOverwrite(e))
				continue;
			    res = plpRfsv->freplacefile(
				plpRfsv->opMode(rfsv::PSI_O_RDWR), fn, handle);
			}
			if (res != rfsv::E_PSI_GEN_NONE) {
			    KMessageBox::error(this, i18n(
				"<QT>Could not create<BR/>"
				"<B>%1</B><BR/>Reason: %2</QT>").arg(fn).arg(KGlobal::locale()->translate(res)));
			    continue;
			}
			const unsigned char *data =
			    (const unsigned char *)(ba.data());
			long len = ba.size();

			do {
			    u_int32_t written;
			    u_int32_t count =
				(len > RFSV_SENDLEN) ? RFSV_SENDLEN : len;
			    res = plpRfsv->fwrite(handle, data, count, written);
			    if (res != rfsv::E_PSI_GEN_NONE)
				break;
			    len -= written;
			    data += written;
			    updateProgress(written);
			} while (len > 0);
			plpRfsv->fclose(handle);
			if (res != rfsv::E_PSI_GEN_NONE) {
			    KMessageBox::error(this, i18n(
				"<QT>Could not write to<BR/>"
				"<B>%1</B><BR/>Reason: %2</QT>").arg(fn).arg(KGlobal::locale()->translate(res)));
			    continue;
			}
			u_int32_t oldattr;
			res = plpRfsv->fgetattr(fn, oldattr);
			if (res != rfsv::E_PSI_GEN_NONE) {
			    KMessageBox::error(this, i18n(
				"<QT>Could not get attributes of<BR/>"
				"<B>%1</B><BR/>Reason: %2</QT>").arg(fn).arg(KGlobal::locale()->translate(res)));
			    continue;
			}
			u_int32_t mask = e.getAttr() ^ oldattr;
			u_int32_t sattr = e.getAttr() & mask;
			u_int32_t dattr = ~sattr & mask;
			res = plpRfsv->fsetattr(fn, sattr, dattr);
			if (res != rfsv::E_PSI_GEN_NONE) {
			    KMessageBox::error(this, i18n(
				"<QT>Could not set attributes of<BR/>"
				"<B>%1</B><BR/>Reason: %2</QT>").arg(fn).arg(KGlobal::locale()->translate(res)));
			    continue;
			}
			res = plpRfsv->fsetmtime(fn, e.getPsiTime());
			if (res != rfsv::E_PSI_GEN_NONE) {
			    KMessageBox::error(this, i18n(
				"<QT>Could not set modification time of<BR/>"
				"<B>%1</B><BR/>Reason: %2</QT>").arg(fn).arg(KGlobal::locale()->translate(res)));
			    continue;
			}
			overWriteList += QString(fn);
		    }
		}
		tgz.close();
	    }
	}
	// Restart previously running applications on the Psion
	// from saved state info.
	runRestore();
    } else {
	restoreRunning = false;
	switchActions();
	statusBar()->changeItem(i18n("Connected to %1").arg(machineName),
				STID_CONNECTION);
	return;
    }

    restoreRunning = false;
    switchActions();
    emit setProgress(0);
    emit enableProgressText(false);
    statusBar()->changeItem(i18n("Connected to %1").arg(machineName),
			    STID_CONNECTION);
    statusBar()->message(i18n("Restore done"), 2000);
}

void KPsionMainWindow::
slotStartFormat() {
    if (KMessageBox::warningYesNo(this, i18n(
				      "<QT>This erases <B>ALL</B> data "
				      "on the drive(s).<BR/>Do you really "
				      "want to proceed?"
				      )) == KMessageBox::No)
	return;
    formatRunning = true;
    switchActions();
}

void KPsionMainWindow::
slotToggleToolbar() {
    if (toolBar()->isVisible())
	toolBar()->hide();
    else
	toolBar()->show();
}

void KPsionMainWindow::
slotToggleStatusbar() {
    if (statusBar()->isVisible())
	statusBar()->hide();
    else
	statusBar()->show();
}

void KPsionMainWindow::
slotSaveOptions() {
}

void KPsionMainWindow::
slotPreferences() {
}

void KPsionMainWindow::
updateProgress(unsigned long amount) {
    progressLocalCount += amount;
    int lastPercent = progressLocalPercent;
    if (progressLocal)
	progressLocalPercent = progressLocalCount * 100 / progressLocal;
    else
	progressLocalPercent = 100;
    if (progressLocalPercent != lastPercent)
	emit setProgress(progressLocalPercent);
    if (progressTotal > 0) {
	progressCount += amount;
	lastPercent = progressPercent;
	if (progressTotal)
	    progressPercent = progressCount * 100 / progressTotal;
	else
	    progressPercent = 100;
	if (progressPercent != lastPercent)
	    statusBar()->changeItem(progressTotalText.arg(progressPercent),
		STID_CONNECTION);
    }
    kapp->processEvents();
}

void KPsionMainWindow::
collectFiles(QString dir) {
    Enum<rfsv::errs> res;
    PlpDir files;
    QString tmp = dir;

    kapp->processEvents();
    tmp += "\\";
    if ((res = plpRfsv->dir(tmp.data(), files)) != rfsv::E_PSI_GEN_NONE) {
	// messagebox "Couldn't get directory ...."
    } else
	for (int i = 0; i < files.size(); i++) {
	    PlpDirent e = files[i];


	    long attr = e.getAttr();
	    tmp = dir;
	    tmp += "\\";
	    tmp += e.getName();
	    if (attr & rfsv::PSI_A_DIR) {
		collectFiles(tmp);
	    } else {
		updateProgress(e.getSize());
		if ((attr & rfsv::PSI_A_ARCHIVE) || fullBackup) {
		    backupCount++;
		    backupSize += e.getSize();
		    e.setName(tmp.data());
		    toBackup.push_back(e);
		}
	    }
	}
}

void KPsionMainWindow::
killSave() {
    Enum<rfsv::errs> res;
    bufferArray tmp;

    savedCommands.clear();
    if ((res = plpRpcs->queryDrive('C', tmp)) != rfsv::E_PSI_GEN_NONE) {
	cerr << "Could not get process list, Error: " << res << endl;
	return;
    } else {
	while (!tmp.empty()) {
	    QString pbuf;
	    bufferStore cmdargs;
	    bufferStore bs = tmp.pop();
	    int pid = bs.getWord(0);
	    const char *proc = bs.getString(2);
	    if (S5mx)
		pbuf.sprintf("%s.$%02d", proc, pid);
	    else
		pbuf.sprintf("%s.$%d", proc, pid);
	    bs = tmp.pop();
	    if (plpRpcs->getCmdLine(pbuf.data(), cmdargs) == 0) {
		QString cmdline(cmdargs.getString(0));
		cmdline += " ";
		cmdline += bs.getString(0);
		savedCommands += cmdline;
	    }
	    emit setProgressText(i18n("Stopping %1").arg(cmdargs.getString(0)));
	    kapp->processEvents();
	    plpRpcs->stopProgram(pbuf);
	}
    }
    return;
}

void KPsionMainWindow::
runRestore() {
    Enum<rfsv::errs> res;

    for (QStringList::Iterator it = savedCommands.begin(); it != savedCommands.end(); it++) {
	int firstBlank = (*it).find(' ');
	QString cmd = (*it).left(firstBlank);
	QString arg = (*it).mid(firstBlank + 1);

	if (!cmd.isEmpty()) {
	    // Workaround for broken programs like Backlite.
	    // These do not storethe full program path.
	    // In that case we try running the arg1 which
	    // results in starting the program via recog. facility.
	    emit setProgressText(i18n("Starting %1").arg(cmd));
	    kapp->processEvents();
	    if ((arg.length() > 2) && (arg[1] == ':') && (arg[0] >= 'A') &&
		(arg[0] <= 'Z'))
		res = plpRpcs->execProgram(arg.data(), "");
	    else
		res = plpRpcs->execProgram(cmd.data(), arg.data());
	    if (res != rfsv::E_PSI_GEN_NONE) {
		// If we got an error here, that happened probably because
		// we have no path at all (e.g. Macro5) and the program is
		// not registered in the Psion's path properly. Now try
		// the ususal \System\Apps\<AppName>\<AppName>.app
		// on all drives.
		if (cmd.find('\\') == -1) {
		    driveMap::Iterator it;
		    for (it = drives.begin(); it != drives.end(); it++) {
			QString newcmd = QString::fromLatin1("%1:\\System\\Apps\\%2\\%3").arg(it.key()).arg(cmd).arg(cmd);
			res = plpRpcs->execProgram(newcmd.data(), "");
			if (res == rfsv::E_PSI_GEN_NONE)
			    break;
			newcmd += ".app";
			res = plpRpcs->execProgram(newcmd.data(), "");
			if (res == rfsv::E_PSI_GEN_NONE)
			    break;

		    }
		}
	    }
	}
    }
    return;
}

void KPsionMainWindow::
createIndex() {
    QByteArray ba;
    QTextOStream os(ba);
    os << "#plpbackup index " <<
	(fullBackup ? "F" : "I") << endl;
    for (int i = 0; i < toBackup.size(); i++) {
	PlpDirent e = toBackup[i];
	PsiTime t = e.getPsiTime();
	long attr = e.getAttr() &
	    ~rfsv::PSI_A_ARCHIVE;
	os << hex
	   << setw(8) << setfill('0') <<
	    t.getPsiTimeHi() << " "
	   << setw(8) << setfill('0') <<
	    t.getPsiTimeLo() << " "
	   << setw(8) << setfill('0') <<
	    e.getSize() << " "
	   << setw(8) << setfill('0') <<
	    attr << " "
	   << setw(0) << e.getName() << endl;
	kapp->processEvents();
    }
    QString idxName =
	QString::fromLatin1("KPsion%1Index").arg(fullBackup ?
						 "Full" : "Incremental");
    backupTgz->writeFile(idxName, "root", "root", ba.size(), ba.data());
}

/*
 * Local variables:
 * c-basic-offset: 4
 * End:
 */
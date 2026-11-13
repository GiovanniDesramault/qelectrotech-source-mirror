/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "ElementsCollection/elementslocation.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "diagram.h"
#include "diagramcontext.h"
#include "factory/elementfactory.h"
#include "qetapp.h"
#include "qetapptest.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/independenttextitem.h"
#include "qetinformation.h"
#include "qetproject.h"
#include "tecside/tecsidelocations.h"
#include "tecside/tecsiderack.h"
#include "tecside/tecsiderackdrawer.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QUndoStack>
#include <QtTest>

using namespace TecsideRack;

/**
	TECside Draw rack for location: the layout of a rack (amplifiers at
	the bottom and never touching, frames, shelves, rows, the devices
	without a front), and the new folio drawn from the company
	collection's rack symbols.
*/
class tst_tecsiderack : public QObject
{
	Q_OBJECT

	const QList<int> frames{6, 12, 16, 24, 42};

	static TecsideRack::Device rack(const QString &name, int units, bool amp = false,
									qreal width = rackWidth)
	{
		TecsideRack::Device device;
		device.name = name;
		device.units = units;
		device.size = QSizeF(width, units * unit);
		device.isAmp = amp;
		return device;
	}

	static TecsideRack::Device desktop(const QString &name, qreal width, qreal height)
	{
		TecsideRack::Device device;
		device.name = name;
		device.size = QSizeF(width, height);
		return device;
	}

	static TecsideRack::Device noFront(const QString &name)
	{
		TecsideRack::Device device;
		device.name = name;
		device.hasFront = false;
		return device;
	}

	static QPointF posOf(const Layout &layout, int device)
	{
		const Placement *placement = layout.placementOf(device);
		return placement ? placement->pos : QPointF(-1, -1);
	}

		//The company collection of the test, in its temporary data folder
	static QString company()
	{
		return QETApp::companyElementsDir();
	}

	static bool writeSymbol(const QString &relative, qreal width, qreal height,
							const QString &description, const QString &extra_info = QString())
	{
		const QString path = QDir(company()).filePath(relative);
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly))
			return false;
		file.write(QStringLiteral(
			"<definition version=\"0.100.0\" type=\"element\" link_type=\"simple\" "
			"width=\"%1\" height=\"%2\" hotspot_x=\"10\" hotspot_y=\"10\">"
			"<uuid uuid=\"%3\"/><names><name lang=\"en\">%4</name></names>"
			"<elementInformations>"
			"<elementInformation show=\"1\" name=\"description\">%5</elementInformation>%6"
			"</elementInformations><description>"
			"<rect x=\"0\" y=\"0\" width=\"%7\" height=\"%8\" rx=\"0\" ry=\"0\" "
			"style=\"line-style:normal;line-weight:normal;filling:none;color:black\" "
			"antialias=\"false\"/></description></definition>")
			.arg(width + 20).arg(height + 20).arg(QUuid::createUuid().toString(),
			QFileInfo(path).completeBaseName(), description, extra_info)
			.arg(width).arg(height).toUtf8());
		return true;
	}

		//What build_rack.py writes, the few the test needs
	static bool writeRackSymbols()
	{
		bool ok = true;
		for (int units : {6, 12, 16, 24, 42})
			ok &= writeSymbol(QStringLiteral("av/rack/frames/rack_frame_%1u.elmt").arg(units),
							  rackWidth, units * unit,
							  QStringLiteral("%1U 19\" rack, front view").arg(units));
		ok &= writeSymbol(QStringLiteral("av/rack/blind_panels/blind_1u.elmt"),
						  rackWidth, unit, QStringLiteral("1U blank panel"));
		ok &= writeSymbol(QStringLiteral("av/rack/shelves/shelf_1u.elmt"),
						  rackWidth, unit, QStringLiteral("1U rack shelf for desktop units"));
		ok &= writeSymbol(QStringLiteral("av/rack/shelves/shelf_2u.elmt"),
						  rackWidth, 2 * unit, QStringLiteral("2U rack shelf for desktop units"));
		ok &= writeSymbol(QStringLiteral("av/rack/devices/acme/dsp_front.elmt"),
						  rackWidth, unit, QStringLiteral("1U full width"));
		ok &= writeSymbol(QStringLiteral("av/rack/devices/acme/amp_front.elmt"),
						  rackWidth, 2 * unit, QStringLiteral("2U full width"));
		ok &= writeSymbol(QStringLiteral("av/rack/devices/acme/mini_front.elmt"),
						  110, 20, QStringLiteral("desktop unit 237.5 x 35 mm, place on a shelf"));
		return ok;
	}

		//A device of the schematic, embedded as "import/<id>.elmt"
	static Element *placeDevice(QETProject *project, Diagram *diagram, const QString &id,
								const QString &label, const QString &location,
								const QPointF &pos, bool amp = false)
	{
		auto *collection = project->embeddedElementCollection();
		QDomDocument definition;
		definition.setContent(QStringLiteral(
			"<definition type=\"element\" link_type=\"simple\" uuid=\"%1\" "
			"width=\"60\" height=\"40\" hotspot_x=\"30\" hotspot_y=\"20\">"
			"<names><name lang=\"en\">%2</name></names>"
			"<elementInformations>%3</elementInformations>"
			"<description><line x1=\"0\" y1=\"-10\" x2=\"40\" y2=\"-10\"/>"
			"</description></definition>")
			.arg(QUuid::createUuid().toString(), id,
				 amp ? QStringLiteral("<elementInformation show=\"0\" name=\"category\">"
									  "amplifier</elementInformation>")
					 : QString()));
		if (!ElementsLocation(QStringLiteral("embed://import/%1.elmt").arg(id), project).exist()
			&& !collection->addElementDefinition(QStringLiteral("import"), id,
												 definition.documentElement()))
			return nullptr;
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(
			ElementsLocation(QStringLiteral("embed://import/%1.elmt").arg(id), project),
			nullptr, &state);
		if (state || !element) return nullptr;
		element->setPos(pos);
		diagram->addItem(element);
		DiagramContext info = element->elementInformations();
		info.addValue(QETInformation::ELMT_LABEL, label);
		info.addValue(QETInformation::ELMT_LOCATION, location);
		element->setElementInformations(info);
		return element;
	}

	static QString info(const Element *element, const QString &key)
	{
		return element->elementInformations().value(key).toString();
	}

	static QList<Element *> fronts(Diagram *diagram)
	{
		QList<Element *> list;
		for (Element *element : diagram->elements())
			if (TecsideRackDrawer::deviceId(element).endsWith(QLatin1String("_front")))
				list << element;
		std::sort(list.begin(), list.end(), [](Element *a, Element *b) {
			return a->scenePos().y() < b->scenePos().y();
		});
		return list;
	}

	static int countOf(Diagram *diagram, const QString &id)
	{
		int count = 0;
		for (Element *element : diagram->elements())
			if (TecsideRackDrawer::deviceId(element) == id)
				++count;
		return count;
	}

	private slots:
		void ampsAtTheBottomNeverTouching()
		{
			const Layout layout = TecsideRack::layout(
				{rack("AMP1", 1, true), rack("DSP1", 1), rack("AMP2", 2, true),
				 rack("AMP3", 1, true), rack("SW1", 1)}, frames);
				//1 + 1 on top, 1 + 1 + 2 + 1 + 1 at the bottom: 8U, a 12U frame
			QCOMPARE(layout.frames, QList<int>{12});
			QCOMPARE(layout.units, 8);
			QCOMPARE(posOf(layout, 1), QPointF(0, 0));
			QCOMPARE(posOf(layout, 4), QPointF(0, 20));
				//The amplifiers in their order, the last on the bottom unit
			QCOMPARE(posOf(layout, 0), QPointF(0, 120));
			QCOMPARE(posOf(layout, 2), QPointF(0, 160));
			QCOMPARE(posOf(layout, 3), QPointF(0, 220));
			const QList<Placement> blanks = layout.of(Placement::Blank);
			QCOMPARE(blanks.size(), 2);
			QCOMPARE(blanks.at(0).pos, QPointF(0, 140));
			QCOMPARE(blanks.at(1).pos, QPointF(0, 200));
			QCOMPARE(blanks.at(0).units, 1);
		}

		void oneAmpNeedsNoBlank()
		{
			const Layout layout = TecsideRack::layout({rack("AMP1", 2, true)}, frames);
			QVERIFY(layout.of(Placement::Blank).isEmpty());
			QCOMPARE(layout.frames, QList<int>{6});
				//At the bottom of the 6U frame
			QCOMPARE(posOf(layout, 0), QPointF(0, 80));
		}

		void smallestFrameThatFits_data()
		{
			QTest::addColumn<int>("units");
			QTest::addColumn<QList<int>>("expected");
			QTest::newRow("1U") << 1 << QList<int>{6};
			QTest::newRow("6U") << 6 << QList<int>{6};
			QTest::newRow("7U") << 7 << QList<int>{12};
			QTest::newRow("13U") << 13 << QList<int>{16};
			QTest::newRow("17U") << 17 << QList<int>{24};
			QTest::newRow("42U") << 42 << QList<int>{42};
			QTest::newRow("43U") << 43 << QList<int>{42, 6};
			QTest::newRow("60U") << 60 << QList<int>{42, 24};
			QTest::newRow("90U") << 90 << QList<int>{42, 42, 6};
		}

		void smallestFrameThatFits()
		{
			QFETCH(int, units);
			QFETCH(QList<int>, expected);
			QList<TecsideRack::Device> devices;
			for (int i = 0 ; i < units ; ++i)
				devices << rack(QStringLiteral("D%1").arg(i), 1);
			const Layout layout = TecsideRack::layout(devices, frames);
			QCOMPARE(layout.frames, expected);
			QCOMPARE(layout.of(Placement::Frame).size(), expected.size());
		}

		void secondFrameStandsBesideTheFirst()
		{
			QList<TecsideRack::Device> devices;
			for (int i = 0 ; i < 30 ; ++i)
				devices << rack(QStringLiteral("D%1").arg(i), 2);
			const Layout layout = TecsideRack::layout(devices, frames);
			QCOMPARE(layout.frames, (QList<int>{42, 24}));
			QCOMPARE(layout.of(Placement::Frame).at(1).pos, QPointF(framePitch, 0));
				//21 devices of 2U fill the 42U, the 22nd tops the next one
			QCOMPARE(posOf(layout, 20), QPointF(0, 800));
			QCOMPARE(layout.placementOf(20)->frame, 0);
			QCOMPARE(posOf(layout, 21), QPointF(framePitch, 0));
			QCOMPARE(layout.placementOf(21)->frame, 1);
		}

		void noBlankAtTheEdgeOfAFrame()
		{
				//39U, then two amps of 2U: the blank between them would end
				//the first frame, and is left out
			QList<TecsideRack::Device> devices;
			for (int i = 0 ; i < 39 ; ++i)
				devices << rack(QStringLiteral("D%1").arg(i), 1);
			devices << rack(QStringLiteral("AMP1"), 2, true)
					<< rack(QStringLiteral("AMP2"), 2, true);
			const Layout layout = TecsideRack::layout(devices, frames);
			QCOMPARE(layout.frames, (QList<int>{42, 6}));
			QVERIFY(layout.of(Placement::Blank).isEmpty());
			QCOMPARE(layout.placementOf(39)->frame, 0);
			QCOMPARE(layout.placementOf(40)->frame, 1);
			QCOMPARE(posOf(layout, 40), QPointF(framePitch, 80));
		}

		void shelfFillsAndWraps()
		{
				//60 px wide: three fit between the ears (200 px), the
				//fourth starts a new shelf
			const Layout layout = TecsideRack::layout(
				{desktop("PC1", 60, 30), desktop("PC2", 60, 30), desktop("PC3", 60, 30),
				 desktop("PC4", 60, 30), rack("DSP1", 1)}, frames);
			const QList<Placement> shelves = layout.of(Placement::Shelf);
			QCOMPARE(shelves.size(), 2);
				//30 px high: a 2U shelf
			QCOMPARE(shelves.at(0).units, 2);
			QCOMPARE(shelves.at(0).pos, QPointF(0, 0));
			QCOMPARE(posOf(layout, 0), QPointF(ear, 10));
			QCOMPARE(posOf(layout, 1), QPointF(ear + 60, 10));
			QCOMPARE(posOf(layout, 2), QPointF(ear + 120, 10));
			QCOMPARE(shelves.at(1).pos, QPointF(0, 40));
			QCOMPARE(posOf(layout, 3), QPointF(ear, 50));
				//The rack device after the shelves
			QCOMPARE(posOf(layout, 4), QPointF(0, 80));
			QCOMPARE(layout.units, 5);
			QVERIFY(layout.tooWide.isEmpty());
		}

		void lowDesktopUnitsTakeAOneUnitShelf()
		{
				//110 px each: two do not fit between the ears
			const Layout layout = TecsideRack::layout(
				{desktop("ATEM1", 110, 20), desktop("ATEM2", 110, 20)}, frames);
			const QList<Placement> shelves = layout.of(Placement::Shelf);
			QCOMPARE(shelves.size(), 2);
			QCOMPARE(shelves.at(0).units, 1);
			QCOMPARE(shelves.at(1).pos, QPointF(0, 20));
		}

		void desktopUnitWiderThanAShelf()
		{
			const Layout layout = TecsideRack::layout(
				{desktop("BIG", 270, 80), desktop("PC", 60, 30)}, frames);
			QCOMPARE(layout.tooWide, QList<int>{0});
			QCOMPARE(layout.of(Placement::Shelf).size(), 2);
		}

		void narrowRackDevicesSideBySide()
		{
			const Layout layout = TecsideRack::layout(
				{rack("H1", 1, false, 110), rack("H2", 1, false, 110),
				 rack("H3", 1, false, 110)}, frames);
			QCOMPARE(posOf(layout, 0), QPointF(0, 0));
			QCOMPARE(posOf(layout, 1), QPointF(110, 0));
			QCOMPARE(posOf(layout, 2), QPointF(0, 20));
			QVERIFY(layout.of(Placement::Shelf).isEmpty());
		}

		void halfWidthAmpsNeverTouchEither()
		{
			const Layout layout = TecsideRack::layout(
				{rack("AMP1", 1, true, 110), rack("AMP2", 1, true, 110)}, frames);
			QCOMPARE(posOf(layout, 0), QPointF(0, 60));
			QCOMPARE(posOf(layout, 1), QPointF(0, 100));
			QCOMPARE(layout.of(Placement::Blank).size(), 1);
		}

		void devicesWithoutFrontAreListed()
		{
			const Layout layout = TecsideRack::layout(
				{noFront("SPK1"), rack("DSP1", 1), noFront("CAM1")}, frames);
			QCOMPARE(layout.notInRack, (QList<int>{0, 2}));
			QVERIFY(!layout.placementOf(0));
			QCOMPARE(posOf(layout, 1), QPointF(0, 0));

			const Layout none = TecsideRack::layout({noFront("SPK1")}, frames);
			QVERIFY(none.frames.isEmpty());
			QVERIFY(none.placements.isEmpty());
			QCOMPARE(none.notInRack, QList<int>{0});
		}

		void findsTheSymbols()
		{
			QVERIFY(writeRackSymbols());
			const TecsideRackDrawer::Symbols symbols =
				TecsideRackDrawer::findSymbols(company());
			QVERIFY(symbols.isComplete());
			QCOMPARE(symbols.frames.keys(), (QList<int>{6, 12, 16, 24, 42}));
			QCOMPARE(symbols.blinds.keys(), QList<int>{1});
			QCOMPARE(symbols.shelves.keys(), (QList<int>{1, 2}));
			QCOMPARE(symbols.fronts.value(QStringLiteral("dsp")),
					 QStringLiteral("company://av/rack/devices/acme/dsp_front.elmt"));
			QVERIFY(!TecsideRackDrawer::findSymbols(QDir::tempPath()
				+ QStringLiteral("/no_such_collection")).isComplete());
		}

		void drawsTheRackOfALocation()
		{
			QVERIFY(writeRackSymbols());
			QETProject project;
			TecsideLocations locations;
			locations.setEnabled(true);
			locations.setList({{QStringLiteral("03"), QStringLiteral("Regie")},
							   {QStringLiteral("04"), QStringLiteral("FOH")}});
			project.setTecsideLocations(locations);
			Diagram *folio = project.addNewDiagram();
			Diagram *folio2 = project.addNewDiagram();
				//Schematic order: folio, then top to bottom, then left to right
			QVERIFY(placeDevice(&project, folio, QStringLiteral("amp"), QStringLiteral("AMP1"),
								QStringLiteral("03"), QPointF(300, 100), true));
			QVERIFY(placeDevice(&project, folio, QStringLiteral("dsp"), QStringLiteral("DSP1"),
								QStringLiteral("03"), QPointF(100, 100)));
			QVERIFY(placeDevice(&project, folio, QStringLiteral("speaker"), QStringLiteral("SPK1"),
								QStringLiteral("03"), QPointF(100, 300)));
			QVERIFY(placeDevice(&project, folio, QStringLiteral("dsp"), QStringLiteral("DSP9"),
								QStringLiteral("04"), QPointF(100, 400)));
			QVERIFY(placeDevice(&project, folio2, QStringLiteral("amp"), QStringLiteral("AMP2"),
								QStringLiteral("03"), QPointF(100, 0), true));
			QVERIFY(placeDevice(&project, folio2, QStringLiteral("mini"), QStringLiteral("ATEM1"),
								QStringLiteral("03"), QPointF(300, 0)));

			const TecsideRackDrawer::Symbols symbols =
				TecsideRackDrawer::findSymbols(company());
			const int folios = project.folios().size();
			const int index = project.undoStack()->index();
			QString error;
			Diagram *rack = TecsideRackDrawer::draw(&project, QStringLiteral("03"),
													symbols, &error);
			QVERIFY2(rack, qPrintable(error));
			QCOMPARE(project.folios().size(), folios + 1);
			QCOMPARE(project.folios().last(), rack);
			QCOMPARE(rack->border_and_titleblock.title(),
					 QStringLiteral("Rack 03 – Regie"));
				//One undo step
			QCOMPARE(project.undoStack()->index(), index + 1);

				//DSP1, the shelf of ATEM1, then AMP1, a blank, AMP2: 1 + 1 +
				//2 + 1 + 2 = 7U, a 12U frame
			QCOMPARE(countOf(rack, QStringLiteral("rack_frame_12u")), 1);
			QCOMPARE(countOf(rack, QStringLiteral("blind_1u")), 1);
			QCOMPARE(countOf(rack, QStringLiteral("shelf_1u")), 1);
			const QList<Element *> placed = fronts(rack);
			QCOMPARE(placed.size(), 4);
			QStringList labels;
			for (Element *front : placed) {
				labels << info(front, QETInformation::ELMT_LABEL);
				QCOMPARE(info(front, QETInformation::ELMT_LOCATION), QStringLiteral("03"));
			}
			QCOMPARE(labels, (QStringList{QStringLiteral("DSP1"), QStringLiteral("ATEM1"),
										  QStringLiteral("AMP1"), QStringLiteral("AMP2")}));
				//The amplifiers do not touch: a 2U amp, a 1U gap
			QCOMPARE(placed.at(3)->scenePos().y() - placed.at(2)->scenePos().y(), 60.0);
			Element *blank = nullptr;
			for (Element *element : rack->elements())
				if (TecsideRackDrawer::deviceId(element) == QLatin1String("blind_1u"))
					blank = element;
			QCOMPARE(blank->scenePos().y(), placed.at(2)->scenePos().y() + 40);
				//And sit at the bottom of the frame
			Element *frame = nullptr;
			for (Element *element : rack->elements())
				if (TecsideRackDrawer::deviceId(element) == QLatin1String("rack_frame_12u"))
					frame = element;
			QCOMPARE(placed.at(3)->scenePos().y(), frame->scenePos().y() + 200);
			QCOMPARE(placed.at(0)->scenePos(), frame->scenePos());

				//The speaker, without a front, beside the rack
			QList<IndependentTextItem *> texts;
			for (QGraphicsItem *item : rack->items())
				if (auto *text = qgraphicsitem_cast<IndependentTextItem *>(item))
					texts << text;
			QCOMPARE(texts.size(), 1);
			QVERIFY(texts.first()->toPlainText().startsWith(QStringLiteral("Not in rack:")));
			QVERIFY(texts.first()->toPlainText().contains(QStringLiteral("SPK1")));
			QVERIFY(!texts.first()->toPlainText().contains(QStringLiteral("DSP9")));
			QVERIFY(texts.first()->pos().x() > frame->scenePos().x() + rackWidth);

				//The rack is in the folio: the frame reaches no lower than
				//the title block
			const BorderTitleBlock &border = rack->border_and_titleblock;
			QVERIFY(frame->scenePos().y() + 12 * unit < border.titleBlockRect().top());

				//Undone and done again as one
			project.undoStack()->undo();
			QCOMPARE(project.folios().size(), folios);
			project.undoStack()->redo();
			QCOMPARE(project.folios().size(), folios + 1);
			QCOMPARE(fronts(project.folios().last()).size(), 4);
			QCOMPARE(project.folios().last()->border_and_titleblock.title(),
					 QStringLiteral("Rack 03 – Regie"));

				//A second rack counts the devices, not the first rack
			Diagram *again = TecsideRackDrawer::draw(&project, QStringLiteral("03"),
													 symbols, &error);
			QVERIFY2(again, qPrintable(error));
			QCOMPARE(fronts(again).size(), 4);
		}

		void tellsWhatIsMissing()
		{
			QETProject project;
			Diagram *folio = project.addNewDiagram();
			QVERIFY(placeDevice(&project, folio, QStringLiteral("dsp"), QStringLiteral("DSP1"),
								QStringLiteral("03"), QPointF(0, 0)));
			const int folios = project.folios().size();
			QString error;
			QVERIFY(!TecsideRackDrawer::draw(&project, QStringLiteral("03"),
											 TecsideRackDrawer::Symbols(), &error));
			QVERIFY(error.contains(QStringLiteral("rack symbols")));
			QVERIFY(writeRackSymbols());
			QVERIFY(!TecsideRackDrawer::draw(&project, QStringLiteral("07"),
				TecsideRackDrawer::findSymbols(company()), &error));
			QVERIFY(error.contains(QStringLiteral("07")));
			QCOMPARE(project.folios().size(), folios);
		}

		void actionOnlyWhenOn()
		{
			Diagram *diagram = QetAppTest::diagram();
			QVERIFY(diagram);
			QETProject *project = diagram->project();
			QAction *draw = QetAppTest::action(QStringLiteral("draw_rack"));
			QVERIFY(draw);
			QTRY_VERIFY(!draw->isEnabled());
			TecsideLocations locations;
			locations.setEnabled(true);
			locations.setList({{QStringLiteral("01"), QStringLiteral("FOH")}});
			project->setTecsideLocations(locations);
			QTRY_VERIFY(draw->isEnabled());
			project->setTecsideLocations(TecsideLocations());
			QTRY_VERIFY(!draw->isEnabled());
		}
};

QET_APP_TEST_MAIN(tst_tecsiderack)
#include "tst_tecsiderack.moc"

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
#include "qetapptest.h"
#include "qetgraphicsitem/element.h"
#include "qetinformation.h"
#include "qetproject.h"
#include "qetresult.h"
#include "tecside/tecsidelocations.h"

#include <QDomDocument>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

/**
	The TECside places of a project: their XML, saving and opening, the
	box on the sheets, renumbering a place and setting the place of
	several devices at once.
*/
class tst_tecsidelocations : public QObject
{
	Q_OBJECT

		//A project of one folio and @p count devices, without places
	struct Rig
	{
		QETProject project;
		Diagram *diagram = nullptr;
		QList<Element *> devices;

		explicit Rig(int count = 3)
		{
			diagram = project.addNewDiagram();
			for (int i = 0 ; i < count ; ++i) {
				Element *element = device(QStringLiteral("device%1").arg(i));
				element->setPos(i * 100, 0);
				devices << element;
			}
		}

		Element *device(const QString &name)
		{
			auto *collection = project.embeddedElementCollection();
			QDomDocument definition;
			definition.setContent(QStringLiteral(
				"<definition type=\"element\" link_type=\"simple\" uuid=\"%1\" "
				"width=\"60\" height=\"40\" hotspot_x=\"30\" hotspot_y=\"20\">"
				"<names><name lang=\"en\">%2</name></names>"
				"<description><line x1=\"0\" y1=\"-10\" x2=\"40\" y2=\"-10\"/>"
				"</description></definition>")
				.arg(QUuid::createUuid().toString(), name));
			if (!collection->addElementDefinition(QStringLiteral("import"),
					name, definition.documentElement()))
				return nullptr;
			int state = 0;
			Element *element = ElementFactory::Instance()->createElement(
				ElementsLocation(QStringLiteral("embed://import/%1.elmt").arg(name),
								 &project),
				nullptr, &state);
			if (state || !element) return nullptr;
			diagram->addItem(element);
			return element;
		}

		static void setLocation(Element *element, const QString &location)
		{
			DiagramContext info = element->elementInformations();
			info.addValue(QETInformation::ELMT_LOCATION, location);
			element->setElementInformations(info);
		}

		static QString location(Element *element)
		{
			return element->elementInformations()
					.value(QETInformation::ELMT_LOCATION).toString();
		}
	};

	static TecsideLocations sample()
	{
		TecsideLocations locations;
		locations.setEnabled(true);
		locations.setList({{QStringLiteral("01"), QStringLiteral("FOH")},
						   {QStringLiteral("02"), QStringLiteral("Podium L")}});
		return locations;
	}

	static TecsideLocations reopen(QETProject &project, const QString &path)
	{
		project.setFilePath(path);
		if (!project.write().isOk())
			return TecsideLocations();
		QETProject reopened(path);
		return reopened.tecsideLocations();
	}

	private slots:
		void xmlRoundTrip()
		{
			QDomDocument document;
			const QDomElement xml = sample().toXml(document);
			QCOMPARE(xml.tagName(), QStringLiteral("tecside_locations"));
			QCOMPARE(xml.elementsByTagName(QStringLiteral("location")).count(), 2);
			QCOMPARE(xml.firstChildElement().attribute(QStringLiteral("number")),
					 QStringLiteral("01"));
			QCOMPARE(xml.firstChildElement().attribute(QStringLiteral("name")),
					 QStringLiteral("FOH"));
			QCOMPARE(TecsideLocations::fromXml(xml), sample());
		}

		void emptyButOnIsNotAbsent()
		{
			QDomDocument document;
			TecsideLocations on;
			on.setEnabled(true);
			const TecsideLocations read = TecsideLocations::fromXml(on.toXml(document));
			QVERIFY(read.isEnabled());
			QCOMPARE(read.count(), 0);
			QVERIFY(!TecsideLocations::fromXml(QDomElement()).isEnabled());
		}

		void nextFreeNumber()
		{
			TecsideLocations locations;
			QCOMPARE(locations.nextFreeNumber(), QStringLiteral("01"));
			locations.setList({{QStringLiteral("01"), QString()},
							   {QStringLiteral("3"), QString()}});
			QCOMPARE(locations.nextFreeNumber(), QStringLiteral("02"));
			locations.setList({{QStringLiteral("01"), QString()},
							   {QStringLiteral("02"), QString()}});
			QCOMPARE(locations.nextFreeNumber(), QStringLiteral("03"));
			QCOMPARE(TecsideLocations::normalizedNumber(QStringLiteral(" 7 ")),
					 QStringLiteral("07"));
			QCOMPARE(TecsideLocations::normalizedNumber(QStringLiteral("12")),
					 QStringLiteral("12"));
		}

		void saveAndReload_data()
		{
			QTest::addColumn<QString>("file");
			QTest::newRow("qet") << QStringLiteral("places.qet");
			QTest::newRow("qetz") << QStringLiteral("places.qetz");
		}

		void saveAndReload()
		{
			QFETCH(QString, file);
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			Rig r(1);
			r.project.setTecsideLocations(sample());
			QCOMPARE(reopen(r.project, dir.filePath(file)), sample());
		}

		void emptyButOnSurvivesReload_data() { saveAndReload_data(); }

		void emptyButOnSurvivesReload()
		{
			QFETCH(QString, file);
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			Rig r(1);
			TecsideLocations on;
			on.setEnabled(true);
			r.project.setTecsideLocations(on);
			const TecsideLocations read = reopen(r.project, dir.filePath(file));
			QVERIFY(read.isEnabled());
			QCOMPARE(read.count(), 0);
		}

		void absentStaysAbsent()
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());
			const QString path = dir.filePath(QStringLiteral("plain.qet"));
			Rig r(1);
			QVERIFY(!reopen(r.project, path).isEnabled());
			QFile file(path);
			QVERIFY(file.open(QIODevice::ReadOnly));
			QVERIFY(!file.readAll().contains("tecside_locations"));
		}

		void changeMarksModified()
		{
			Rig r(1);
			r.project.setModified(false);
			QVERIFY(!r.project.projectOptionsWereModified());
			r.project.setTecsideLocations(sample());
			QVERIFY(r.project.projectOptionsWereModified());
		}

		void boxRowCount()
		{
			QCOMPARE(TecsideLocations::boxRowCount(0), 5);
			QCOMPARE(TecsideLocations::boxRowCount(2), 5);
			QCOMPARE(TecsideLocations::boxRowCount(5), 5);
			QCOMPARE(TecsideLocations::boxRowCount(7), 7);
			const QStringList rows = sample().boxRows();
			QCOMPARE(rows.size(), 5);
			QCOMPARE(rows.at(0), QStringLiteral("01 FOH"));
			QCOMPARE(rows.at(1), QStringLiteral("02 Podium L"));
			QVERIFY(rows.at(4).isEmpty());
		}

		void boxSitsAboveTitleBlock()
		{
			Rig r(0);
			const BorderTitleBlock &border = r.diagram->border_and_titleblock;
			QVERIFY(border.titleBlockIsDisplayed());
			const QRectF title_block = border.titleBlockRect();
			const QRectF box = sample().boxRect(border);
			QCOMPARE(box.bottom(), title_block.top());
			QCOMPARE(box.right(), title_block.right());
			QVERIFY(box.width() >= 160);
			QCOMPARE(box.height(), 12.0 * 6);
		}

		void boxIsDrawnOnlyWhenOn()
		{
			Rig r(0);
			const QRectF box = sample().boxRect(r.diagram->border_and_titleblock);
			auto inked = [&r, &box]() {
				const QRectF scene = r.diagram->border_and_titleblock.borderAndTitleBlockRect();
				QImage image(scene.size().toSize(), QImage::Format_ARGB32);
				image.fill(Qt::white);
				QPainter painter(&image);
				r.diagram->render(&painter, QRectF(QPointF(0, 0), scene.size()), scene);
				painter.end();
					//A pixel of the header row, left of its text
				const QPoint corner = (box.topLeft() - scene.topLeft()).toPoint();
				int dark = 0;
				for (int x = corner.x() + 1 ; x < corner.x() + box.width() - 1 ; ++x)
					if (qGray(image.pixel(x, corner.y() + 12)) < 128) ++dark;
				return dark;
			};
			const int off = inked();
			r.project.setTecsideLocations(sample());
			const int on = inked();
				//The line under the header runs the whole width
			QVERIFY2(on > off + box.width() / 2,
					 qPrintable(QStringLiteral("off %1 on %2").arg(off).arg(on)));
		}

		void renumberUpdatesElementsAndUndoRestores()
		{
			Rig r(3);
			Rig::setLocation(r.devices.at(0), QStringLiteral("01"));
			Rig::setLocation(r.devices.at(1), QStringLiteral("02"));
			Rig::setLocation(r.devices.at(2), QStringLiteral("05"));
			const TecsideLocations old_locations = sample();
			r.project.setTecsideLocations(old_locations);
			TecsideLocations new_locations = old_locations;
			new_locations.setList({{QStringLiteral("02"), QStringLiteral("FOH")},
								   {QStringLiteral("01"), QStringLiteral("Podium L")}});

				//The two places swap their numbers, in one step
			auto *macro = new QUndoCommand(QStringLiteral("Manage locations"));
			new ChangeTecsideLocationsCommand(&r.project, old_locations, new_locations, macro);
			QVERIFY(TecsideLocationEdit::renumber(&r.project,
				{{QStringLiteral("01"), QStringLiteral("02")},
				 {QStringLiteral("02"), QStringLiteral("01")}}, macro));
			const int index = r.project.undoStack()->index();
			r.project.undoStack()->push(macro);
			QCOMPARE(r.project.undoStack()->index(), index + 1);
			QCOMPARE(Rig::location(r.devices.at(0)), QStringLiteral("02"));
			QCOMPARE(Rig::location(r.devices.at(1)), QStringLiteral("01"));
			QCOMPARE(Rig::location(r.devices.at(2)), QStringLiteral("05"));
			QCOMPARE(r.project.tecsideLocations(), new_locations);

			r.project.undoStack()->undo();
			QCOMPARE(Rig::location(r.devices.at(0)), QStringLiteral("01"));
			QCOMPARE(Rig::location(r.devices.at(1)), QStringLiteral("02"));
			QCOMPARE(Rig::location(r.devices.at(2)), QStringLiteral("05"));
			QCOMPARE(r.project.tecsideLocations(), old_locations);
		}

		void renumberWithoutUsersChangesNoElement()
		{
			Rig r(1);
			Rig::setLocation(r.devices.at(0), QStringLiteral("05"));
			QVERIFY(!TecsideLocationEdit::renumber(&r.project,
				{{QStringLiteral("01"), QStringLiteral("02")}}));
		}

		void undoOfEnablingTurnsOff()
		{
			Rig r(0);
			r.project.undoStack()->push(new ChangeTecsideLocationsCommand(
				&r.project, r.project.tecsideLocations(), sample()));
			QVERIFY(r.project.tecsideLocations().isEnabled());
			r.project.undoStack()->undo();
			QVERIFY(!r.project.tecsideLocations().isEnabled());
		}

		void bulkSetAndUndo()
		{
			Rig r(3);
			Rig::setLocation(r.devices.at(0), QStringLiteral("01"));
			Rig::setLocation(r.devices.at(2), QStringLiteral("02"));
			const int index = r.project.undoStack()->index();
			QUndoCommand *command = TecsideLocationEdit::setLocation(
				r.devices, QStringLiteral("07"));
			QVERIFY(command);
			r.project.undoStack()->push(command);
			QCOMPARE(r.project.undoStack()->index(), index + 1);
			for (Element *element : r.devices)
				QCOMPARE(Rig::location(element), QStringLiteral("07"));
			r.project.undoStack()->undo();
			QCOMPARE(Rig::location(r.devices.at(0)), QStringLiteral("01"));
			QCOMPARE(Rig::location(r.devices.at(1)), QString());
			QCOMPARE(Rig::location(r.devices.at(2)), QStringLiteral("02"));
				//Nothing to do when all are there already
			r.project.undoStack()->redo();
			QVERIFY(!TecsideLocationEdit::setLocation(r.devices, QStringLiteral("07")));
		}
};

QET_APP_TEST_MAIN(tst_tecsidelocations)
#include "tst_tecsidelocations.moc"

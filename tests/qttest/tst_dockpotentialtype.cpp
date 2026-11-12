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
/*
	The Selection properties dock offers the project's potential types to
	the wires it edits: picking one restyles every selected wire in one
	undo step, and wires that disagree show a blank choice that changes
	nothing until a type is picked.
*/
#include "ElementsCollection/elementslocation.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "qetapptest.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetproject.h"
#include "ui/conductorpropertieseditorwidget.h"

#include <QComboBox>
#include <QDomDocument>
#include <QUndoStack>
#include <QUuid>
#include <QtTest>

class tst_dockpotentialtype : public QObject
{
	Q_OBJECT

	static Element *symbol(Diagram *diagram, QPointF at)
	{
		auto *collection = diagram->project()->embeddedElementCollection();
		if (!collection->exist(QStringLiteral("import/simple.elmt"))) {
			QDomDocument definition;
			definition.setContent(QString(
				R"(<definition type="element" link_type="simple" uuid="%1" width="20" height="40" hotspot_x="10" hotspot_y="20">
<names><name lang="en">Test symbol</name></names>
<description><line x1="0" y1="-10" x2="0" y2="10"/>
<terminal x="0" y="-20" orientation="n" uuid="%2"/><terminal x="0" y="20" orientation="s" uuid="%3"/>
</description></definition>)").arg(QUuid::createUuid().toString(),
				QUuid::createUuid().toString(), QUuid::createUuid().toString()));
			if (!collection->addElementDefinition(QStringLiteral("import"),
					QStringLiteral("simple"), definition.documentElement()))
				return nullptr;
		}
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(
			ElementsLocation(QStringLiteral("embed://import/simple.elmt"),
							 diagram->project()),
			nullptr, &state);
		if (state || !element) return nullptr;
		diagram->addItem(element);
		element->setPos(at);
		return element;
	}

		//A wire of its own, typed @p type
	static Conductor *wire(Diagram *d, QPointF at, const QString &type)
	{
		Element *a = symbol(d, at);
		Element *b = symbol(d, at + QPointF(0, 60));
		auto *c = new Conductor(a->terminals().last(), b->terminals().first());
		d->addItem(c);
		ConductorProperties p = c->properties();
		p.m_potential_type = type;
		c->setProperties(p);
		return c;
	}

	static void useTypes(QETProject &project)
	{
		PotentialTypes types;
		types.setEnabled(true);
		types.setTypes({{QStringLiteral("NET"), QColor(0, 0, 255), Qt::SolidLine, 2},
						{QStringLiteral("SDI"), QColor(200, 0, 0), Qt::SolidLine, 2}});
		project.setPotentialTypes(types);
	}

	static QComboBox *combo(QWidget &editor)
	{
		return editor.findChild<QComboBox *>(
			QStringLiteral("m_potential_type_cb"));
	}

	static void choose(QWidget &editor, const QString &type)
	{
		QComboBox *cb = combo(editor);
		cb->setCurrentIndex(cb->findData(type));
		emit cb->activated(cb->currentIndex());
	}

	private slots:
		void initTestCase()
		{
			QSettings().setValue(
				QStringLiteral("diagrameditor/conductor_apply_all"), false);
		}

		void comboVisibleForSelection()
		{
			QETProject project;
			useTypes(project);
			Diagram *d = project.addNewDiagram();
			Conductor *a = wire(d, {100, 100}, QStringLiteral("NET"));
			Conductor *b = wire(d, {300, 100}, QStringLiteral("NET"));
			ConductorPropertiesEditorWidget editor({a, b});
			editor.show();
			QVERIFY(combo(editor));
			QVERIFY(combo(editor)->isVisible());
			QCOMPARE(combo(editor)->currentData().toString(),
					 QStringLiteral("NET"));
		}

		void comboHiddenWithoutTypes()
		{
			QETProject project;
			Diagram *d = project.addNewDiagram();
			Conductor *a = wire(d, {100, 100}, QString());
			ConductorPropertiesEditorWidget editor({a});
			editor.show();
			QVERIFY(!combo(editor)->isVisible());
		}

		void pickingSetsAllSelected()
		{
			QETProject project;
			useTypes(project);
			Diagram *d = project.addNewDiagram();
			Conductor *a = wire(d, {100, 100}, QStringLiteral("NET"));
			Conductor *b = wire(d, {300, 100}, QStringLiteral("NET"));
			ConductorPropertiesEditorWidget editor({a, b});
			editor.setLiveEdit(true);
			editor.show();
			const int before = d->undoStack().count();

			choose(editor, QStringLiteral("SDI"));

			QCOMPARE(a->properties().m_potential_type, QStringLiteral("SDI"));
			QCOMPARE(b->properties().m_potential_type, QStringLiteral("SDI"));
			QCOMPARE(d->undoStack().count(), before + 1);
			d->undoStack().undo();
			QCOMPARE(a->properties().m_potential_type, QStringLiteral("NET"));
			QCOMPARE(b->properties().m_potential_type, QStringLiteral("NET"));
		}

		void mixedShowsBlank()
		{
			QETProject project;
			useTypes(project);
			Diagram *d = project.addNewDiagram();
			Conductor *a = wire(d, {100, 100}, QStringLiteral("NET"));
			Conductor *b = wire(d, {300, 100}, QStringLiteral("SDI"));
			ConductorPropertiesEditorWidget editor({a, b});
			editor.setLiveEdit(true);
			editor.show();
			const int before = d->undoStack().count();

			QCOMPARE(combo(editor)->currentIndex(), -1);
			QVERIFY(editor.associatedUndo() == nullptr);
			QCOMPARE(d->undoStack().count(), before);
			QCOMPARE(a->properties().m_potential_type, QStringLiteral("NET"));
			QCOMPARE(b->properties().m_potential_type, QStringLiteral("SDI"));

				//A choice, even the type of the first wire, reaches both
			choose(editor, QStringLiteral("NET"));
			QCOMPARE(a->properties().m_potential_type, QStringLiteral("NET"));
			QCOMPARE(b->properties().m_potential_type, QStringLiteral("NET"));
			QCOMPARE(d->undoStack().count(), before + 1);
		}
};

QET_APP_TEST_MAIN(tst_dockpotentialtype)
#include "tst_dockpotentialtype.moc"

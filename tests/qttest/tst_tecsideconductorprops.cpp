/*
	TECside cable numbering adds four properties to a wire: the number
	switch, text at both ends, and the sequence of a typed cable. They
	survive the XML, an old file gains none of them on save, and the
	Selection properties dock leaves them alone for wires that disagree
	until the user clicks the box.
*/
#include "ElementsCollection/elementslocation.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "conductorproperties.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "qetapptest.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetproject.h"
#include "ui/conductorpropertieseditorwidget.h"

#include <QCheckBox>
#include <QDomDocument>
#include <QLineEdit>
#include <QUndoStack>
#include <QUuid>
#include <QtTest>

class tst_tecsideconductorprops : public QObject
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

	static QCheckBox *box(QWidget &editor, const char *name)
	{
		return editor.findChild<QCheckBox *>(QLatin1String(name));
	}

	private slots:
		void initTestCase()
		{
			QSettings().setValue(
				QStringLiteral("diagrameditor/conductor_apply_all"), false);
		}

		void defaultsOff()
		{
			ConductorProperties p;
			QVERIFY(!p.m_tecside_number);
			QVERIFY(!p.m_text_at_ends);
			QCOMPARE(p.m_type_seq, -1);
			QVERIFY(p.m_type_seq_of.isEmpty());
		}

		void roundTrip()
		{
			ConductorProperties p;
			p.m_tecside_number = true;
			p.m_text_at_ends = true;
			p.m_type_seq = 7;
			p.m_type_seq_of = QStringLiteral("H07V-K");
			QDomDocument doc;
			QDomElement e = doc.createElement(QStringLiteral("conductor"));
			p.toXml(e);
			QCOMPARE(e.attribute(QStringLiteral("tecside_number")),
					 QStringLiteral("1"));
			QCOMPARE(e.attribute(QStringLiteral("text_at_ends")),
					 QStringLiteral("1"));
			ConductorProperties q;
			q.fromXml(e);
			QVERIFY(q.m_tecside_number);
			QVERIFY(q.m_text_at_ends);
			QCOMPARE(q.m_type_seq, 7);
			QCOMPARE(q.m_type_seq_of, QStringLiteral("H07V-K"));
			QCOMPARE(q.attributes(), p.attributes());
		}

		void oldFileWritesNoNewAttributes()
		{
			QDomDocument doc;
			QDomElement e = doc.createElement(QStringLiteral("conductor"));
			e.setAttribute(QStringLiteral("type"), QStringLiteral("multi"));
			e.setAttribute(QStringLiteral("num"), QStringLiteral("12"));
			ConductorProperties p;
			p.fromXml(e);
			QVERIFY(!p.m_tecside_number);
			QCOMPARE(p.m_type_seq, -1);
			QDomElement out = doc.createElement(QStringLiteral("conductor"));
			p.toXml(out);
			for (const char *name : {"tecside_number", "text_at_ends",
									 "type_seq", "type_seq_of"})
				QVERIFY2(!out.hasAttribute(QLatin1String(name)), name);
		}

		void differingAreNotEqual()
		{
			ConductorProperties a, b;
			b.m_text_at_ends = true;
			QVERIFY(!(a == b));
			b.m_text_at_ends = false;
			b.m_tecside_number = true;
			QVERIFY(!(a == b));
			b.m_tecside_number = false;
			b.m_type_seq = 2;
			QVERIFY(!(a == b));
			b.m_type_seq = -1;
			b.m_type_seq_of = QStringLiteral("X");
			QVERIFY(!(a == b));
		}

		void multiEditKeepsMixed()
		{
			ConductorProperties a, b;
			b.m_text_at_ends = true;
			ConductorProperties target;
			target.m_text_at_ends = true;
			target.applyForEqualAttributes({a, b});
			QVERIFY(target.m_text_at_ends);

			ConductorProperties same_a, same_b;
			same_a.m_tecside_number = same_b.m_tecside_number = true;
			ConductorProperties t2;
			t2.applyForEqualAttributes({same_a, same_b});
			QVERIFY(t2.m_tecside_number);
		}

		void applyChangesOnlyTakesWhatChanged()
		{
			ConductorProperties before, after;
			after.m_text_at_ends = true;
			ConductorProperties mine;
			mine.m_tecside_number = true;
			mine.m_type_seq = 4;
			mine.applyChanges(before, after);
			QVERIFY(mine.m_text_at_ends);
			QVERIFY(mine.m_tecside_number);
			QCOMPARE(mine.m_type_seq, 4);
		}

		void dockSingleWire()
		{
			QETProject project;
			Diagram *d = project.addNewDiagram();
			Conductor *a = wire(d, {100, 100}, QString());
			ConductorPropertiesEditorWidget editor({a});
			editor.setLiveEdit(true);
			editor.show();
			QCheckBox *ends = box(editor, "m_text_at_ends_cb");
			QVERIFY(ends);
			QVERIFY(box(editor, "m_tecside_number_cb"));
			QVERIFY(!ends->isChecked());
			ends->click();
			QVERIFY(a->properties().m_text_at_ends);
			QVERIFY(!a->properties().m_tecside_number);
			d->undoStack().undo();
			QVERIFY(!a->properties().m_text_at_ends);
		}

		void dockMixedChangesNothingUntilClicked()
		{
			QETProject project;
			Diagram *d = project.addNewDiagram();
			Conductor *a = wire(d, {100, 100}, QString());
			Conductor *b = wire(d, {300, 100}, QString());
			ConductorProperties p = b->properties();
			p.m_text_at_ends = true;
			b->setProperties(p);
			ConductorPropertiesEditorWidget editor({a, b});
			editor.setLiveEdit(true);
			editor.show();
			QCheckBox *ends = box(editor, "m_text_at_ends_cb");
			const int before = d->undoStack().count();
			QCOMPARE(ends->checkState(), Qt::PartiallyChecked);
			QVERIFY(editor.associatedUndo() == nullptr);
			QCOMPARE(d->undoStack().count(), before);
			QVERIFY(!a->properties().m_text_at_ends);
			QVERIFY(b->properties().m_text_at_ends);

			ends->click();
			QCOMPARE(ends->checkState(), Qt::Checked);
			QVERIFY(a->properties().m_text_at_ends);
			QVERIFY(b->properties().m_text_at_ends);
			QCOMPARE(d->undoStack().count(), before + 1);
			ends->click();
			QCOMPARE(ends->checkState(), Qt::Unchecked);
			QVERIFY(!a->properties().m_text_at_ends);
			QVERIFY(!b->properties().m_text_at_ends);
		}

		void numberLocksText()
		{
				//The refresh writes the text of a numbered wire over
				//anything typed in: the field says so instead
			QETProject project;
			Diagram *d = project.addNewDiagram();
			Conductor *a = wire(d, {100, 100}, QString());
			ConductorPropertiesEditorWidget editor({a});
			editor.setLiveEdit(true);
			editor.show();
			QCheckBox *number = box(editor, "m_tecside_number_cb");
			auto *text = editor.findChild<QLineEdit *>(
					QStringLiteral("m_text_le"));
			QVERIFY(number && text);
			QVERIFY(text->isEnabled());
			number->click();
			QVERIFY(a->properties().m_tecside_number);
			QVERIFY(!text->isEnabled());
			QVERIFY(!text->toolTip().isEmpty());
			number->click();
			QVERIFY(text->isEnabled());
		}
};

QET_APP_TEST_MAIN(tst_tecsideconductorprops)
#include "tst_tecsideconductorprops.moc"

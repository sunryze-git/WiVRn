#!/bin/sh

cd $(dirname $0)/..

WYRM_CLIENT_POT=$(mktemp)
WYRM_DASHBOARD_POT=$(mktemp)

xgettext \
	--c++ --from-code=UTF-8              \
	--keyword=_:1,1t                     \
	--keyword=_S:1,1t                    \
	--keyword=_C:1c,2,2t                 \
	--keyword=_cS:1c,2,2t                \
	--keyword=_cF:1c,2,2t                \
	--keyword=_F:1,1t                    \
	--keyword=gettext_noop:1,1t          \
	--output=$WYRM_CLIENT_POT           \
	--package-name=Wyrm                 \
	$(find client/ -name "*.cpp" | sort)

xgettext \
	--c++ --kde --from-code=UTF-8        \
	--keyword=i18n:1                     \
	--keyword=i18nc:1c,2                 \
	--keyword=i18np:1,2                  \
	--keyword=i18ncp:1c,2,3              \
	--output=$WYRM_DASHBOARD_POT        \
	--package-name=Wyrm-dashboard       \
	$(find dashboard/ -name "*.qml" -o -name "*.cpp" | sort)

sed -i 's/charset=CHARSET/charset=UTF-8/g' $WYRM_CLIENT_POT $WYRM_DASHBOARD_POT

if [ "$#" -ge 1 ] ; then
	LANGS="$@"
else
	LANGS="$(ls -1 locale)"
fi

for i in $LANGS
do
	mkdir -p locale/$i

	if [ ! -f locale/$i/wyrm.po ]
	then
		cp $WYRM_CLIENT_POT locale/$i/wyrm.po
	else
		msgmerge --quiet --no-fuzzy-matching --update locale/$i/wyrm.po $WYRM_CLIENT_POT
	fi

	if [ ! -f locale/$i/wyrm-dashboard.po ]
	then
		cp $WYRM_DASHBOARD_POT locale/$i/wyrm-dashboard.po
	else
		msgmerge --quiet --no-fuzzy-matching --update locale/$i/wyrm-dashboard.po $WYRM_DASHBOARD_POT
	fi
done

rm $WYRM_CLIENT_POT $WYRM_DASHBOARD_POT

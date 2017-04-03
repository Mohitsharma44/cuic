#! /bin/bash

YEST_DATE=$(date '+%Y-%m-%d' -d "yesterday")
TODAY_DATE=$(date '+%Y-%m-%d')
START_TIME="14:00"
END_TIME="20:00"

SOURCE=${AUDUBON_DATA}"/audubon_imgs_live/"
DESTINATION=${AUDUBON_DATA}/$YEST_DATE"_night_test"

echo "Finding *.raw and *.hdr for $DESTINATION"
mkdir -p $DESTINATION
find $SOURCE -type f \( -name "*.raw" -o -name "*.hdr" \) -newermt "$YEST_DATE $START_TIME" ! -newermt "$TODAY_DATE $END_TIME" -printf %P\\0 | \                                                                   
rsync -av -P --remove-source-files --files-from=- --from0 $SOURCE $DESTINATION | python "$AUDUBON_DATA/rsyncprogress.py"                                                                                           
echo "Done"

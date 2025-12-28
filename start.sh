#!/bin/bash
docker run -d -p 8081:8081 \
-v /home/john/Videos/YT-DL:/downloads \
ghcr.io/alexta69/metube


sudo chown -R $USER:$USER /home/john/Videos/YT-DL

sudo cp 'WATCH THIS Before You Pull in ZZZ for Good Luck! - The Luckiest Gacha Pull EVER! #zenlesszonezero.webm' s-pull-short.webm

ffmpeg -ss 00:00:08 -i s-pull-short.webm -t 4 -c copy segment.mp4

ffmpeg -ss 1 -i segment.mp4 -frames:v 1 frame.png

ffmpeg -i segment.mp4 \
  -vf "drawbox=x=390:y=880:w=360:h=300:color=red@0.7:t=2" \
  preview.mp4 -y

# crop=OUT_W:OUT_H:X:Y
ffmpeg -i segment.mp4 \
  -vf "crop=360:300:390:880" \
  cropped.mp4 -y

ffmpeg -ss 1 -i cropped.mp4 -frames:v 1 frame.png -y


#top-left     (x0,y0)
#top-right    (x1,y1)
#bottom-left  (x2,y2)
#bottom-right (x3,y3)
ffmpeg -i cropped.mp4 \
-vf "perspective=x0=43:y0=17:x1=325:y1=4:x2=16:y2=271:x3=327:y3=271:sense=0" \
undistorted.mp4 -y

ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 undistorted.mp4
#360x300
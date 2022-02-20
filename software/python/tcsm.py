#!/usr/bin/python3
#
# tcsm.py
#
# developend with python 3.8.5
# part of 'trading card sorting machine' (tcsm) project
# https://github.com/olikraus/tcsm
#
# (c) olikraus@gmail.com
# 
# This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
# To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/.
# 


try:
  from picamera import PiCamera
  from picamera.array import PiRGBArray
except ImportError as err:
  print('picamera import error (maybe try: pip3 install picamera):', err);
  
from datetime import datetime
import numpy as np
import cv2
import os
import io
import json
import jellyfish

try:
  import smbus
except ImportError as err:
  print('smbus import error (maybe try: pip3 install smbus):', err);
  
import time
import argparse
import base64
import requests





eject_motor_adr = 0x65
sorter_motor_adr = 0x60


eject_motor_shake_speed = 40     # 0..63
eject_motor_throw_out_speed = 50 # 0..63
eject_motor_throw_out_time = 0.27

sorter_motor_basket_0_1_speed = 15      # 0..63, speed should be very low, however a minimal speed is required (>9)
sorter_motor_basket_0_1_time = 0.25      # time in seconds

sorter_motor_basket_2_3_speed = 63      # 0..63, speed should be very high
sorter_motor_basket_2_3_time = 0.8      # time in seconds

jpeg_full_image_quality=10      # 0..100

quit_word = 'tcsm'

pipename="find_card_pipe"


parser = argparse.ArgumentParser(description='Trading Card Sorting Machine Controller', formatter_class=argparse.RawTextHelpFormatter)
parser.add_argument('-c',
                    default='help',
                    const='help',
                    nargs='?',
                    choices=['sort', 'eb', 'em', 'sm', 'tc', 'cam', 'help'],
                    help='''Define command to execute (default: %(default)s)
    sort: Activate the main purpose of this tool: Sort trading cards
    eb: Eject a card into sorter and move the card into a basket (uses -b and -r)
    em: Eject motor manual control (-ems, -emt, -emd)
    sm: Sorter motor manual control (-sms, -smt, -smd)
    tc: test basket conditions b0c, b1c and b2c
      The condition must be a legal python3 expression. Allowed variables in the conditions:
        tc: Creature?           ta: Artifact?
        ts: Sorcery?            ti: Instant?
        tl: Land?               te: Enchantment?
        tp: Planeswalker?
        cmc: Converted mana cost
        r: rarity 0=common, 1=uncommon, 2=rare, 3=mythic
        cw: Is white card?      cb: Is black card?
        cr: Is red card?        cg: Is green card?
        cu: Is blue card?
''')
parser.add_argument('-log',  action='store', nargs='?',  default='tcsm.log', help='define log filename (default: %(default)s)')
parser.add_argument('-b', 
  action='store',
  nargs='?', 
  default=0,
  const=0,
  type=int,
  help='target basket number')
parser.add_argument('-r',  action='store', nargs='?',  default=1, const=1, type=int, help='repeat count (for -c em)')
parser.add_argument('-b0c',  action='store', nargs='?',  default='tc', help='basket 0 condition (default: %(default)s)')
parser.add_argument('-b1c',  action='store', nargs='?',  default='ts or ti or te', help='basket 1 condition (default: %(default)s)')
parser.add_argument('-b2c',  action='store', nargs='?',  default='tl or ta', help='basket 2 condition (default: %(default)s)')
parser.add_argument('-ems',  action='store', nargs='?',  default=20, const=0, type=int, help='eject motor speed (for -c em)')
parser.add_argument('-emt',  action='store', nargs='?',  default=100, const=0, type=int, help='eject motor time in milliseconds (for -c em)')
parser.add_argument('-emd',  action='store', nargs='?',  default=0, const=0, type=int, help='eject motor direction (for -c em)')
parser.add_argument('-sms',  action='store', nargs='?',  default=20, const=0, type=int, help='sorter motor speed (for -c sm)')
parser.add_argument('-smt',  action='store', nargs='?',  default=100, const=0, type=int, help='sorter motor time in milliseconds (for -c sm)')
parser.add_argument('-smd',  action='store', nargs='?',  default=0, const=0, type=int, help='sorter motor direction (for -c sm)')
parser.add_argument('-key',  action='store', nargs='?',  default='.', help='ocr.space api key')


args = parser.parse_args()

def ocr_space_file(filename, overlay=False, api_key='helloworld', language='eng'):
    """ OCR.space API request with local file.
        Python3.5 - not tested on 2.7
    :param filename: Your file path & name.
    :param overlay: Is OCR.space overlay required in your response.
                    Defaults to False.
    :param api_key: OCR.space API key.
                    Defaults to 'helloworld'.
    :param language: Language code to be used in OCR.
                    List of available language codes can be found on https://ocr.space/OCRAPI
                    Defaults to 'en'.
    :return: Result in JSON format.
    """

    payload = {'isOverlayRequired': overlay,
               'apikey': api_key,
               'language': language,
               }
    with open(filename, 'rb') as f:
        r = requests.post('https://api.ocr.space/parse/image',
                          files={filename: f},
                          data=payload,
                          )
    #return r.content.decode()
    if r.status_code == requests.codes.ok:
      j = r.json();
      return j['ParsedResults'][0]['ParsedText'].partition('\n')[0]
    else:
      print(f"request failed, status code={r.status_code}")
    return "";

# DRV8830
#	Register 0: 	vvvvvvbb
#	Register 1:	c--4321f
#
#	bb = 00		coast
#	bb = 01		reverse
#	bb = 10		forward
#	bb = 11		break
# around one second is required to reach full speed (from 0 to 63)

def motor_coast(adr):
	bus = smbus.SMBus(1)
	bus.write_byte_data(adr, 1, 0x80)	# clear any faults
	bus.write_byte_data(adr, 0, 0)	# send coast

def motor_break(adr):
	bus = smbus.SMBus(1)
	bus.write_byte_data(adr, 1, 0x80)	# clear any faults
	bus.write_byte_data(adr, 0, 3)	# send coast

# dir = 0: reverse, dir = 1: forward
# speed = 0..63
def motor_run(adr,speed,dir):
	bus = smbus.SMBus(1)
	bus.write_byte_data(adr, 1, 0x80)	# clear any faults
	bus.write_byte_data(adr, 0, speed*4 + 1+dir)	# drive

class MCS: # motor control sequence
  def __init__(self, adr, seq):
    self.state = 0
    self.next_time = 0
    self.sequence = seq
    self.jump_back = 0
    self.repeat_cnt = 0
    self.adr = adr
  def start(self):
    self.state = 0
    self.next_time = time.time_ns()
    self.jump_back = 0
    self.repeat_cnt = 0
    return True
  def start_with_sequence(self, seq):
    self.sequence = seq
    self.state = 0
    self.next_time = time.time_ns()
    self.jump_back = 0
    self.repeat_cnt = 0    
  def next(self):
    if self.next_time < time.time_ns():
      if self.state >= len(self.sequence):
        return False
      #print(f"state: {self.state} cmd: {self.sequence[self.state][0]} time: {time.time_ns()/1000000000.0}")
      self.next_time = time.time_ns() + self.sequence[self.state][2]*1000000000.0
      if self.sequence[self.state][0] == 8:
        self.jump_back = self.state + 1
        self.repeat_cnt = self.sequence[self.state][1]
        #print(f"state: {self.state} repeat_cnt set to {self.repeat_cnt}")
        self.state += 1
      elif self.sequence[self.state][0] == 9:
        #print(f"state: {self.state} repeat_cnt: {self.repeat_cnt}")
        if self.repeat_cnt <= 0:
          self.state += 1
        else:
          self.repeat_cnt -= 1
          self.state = self.jump_back
      elif self.sequence[self.state][0] == 0:
        motor_run(self.adr,self.sequence[self.state][1],0)
        self.state += 1
      elif self.sequence[self.state][0] == 1:
        motor_run(self.adr,self.sequence[self.state][1],1)
        self.state += 1
      elif self.sequence[self.state][0] == 2:
        motor_coast(self.adr)
        self.state += 1
      elif self.sequence[self.state][0] == 3:
        motor_break(self.adr)
        self.state += 1
      else:
          self.state += 1
    return True

eject_motor_sequence = [
  [8, 8-1, 0], # while
  [0, eject_motor_shake_speed, 0.05],  # forward
  [3, 0, 0.01],       # break
  [1, eject_motor_shake_speed, 0.05],  # reverse
  [3, 0, 0.01],        # break
  [9, 0, 0], # jump back
  [8, 24-1, 0], # while
  [0, eject_motor_shake_speed, 0.05],  # forward
  [3, 0, 0.01],       # break
  [1, eject_motor_shake_speed, 0.04],  # reverse
  [3, 0, 0.01],        # break
  [9, 0, 0], # jump back
  [0, eject_motor_throw_out_speed, eject_motor_throw_out_time], # eject
  [2, 0, 0.6], # coast
  [1, 25, 0.1], # pullback
  [8, 10-1, 0], # while
  [0, eject_motor_shake_speed, 0.05],  # forward
  [3, 0, 0.01],       # break
  [1, eject_motor_shake_speed, 0.09],  # reverse
  [3, 0, 0.01],        # break
  [9, 0, 0], # jump back
  [1, 25, 0.5], # pullback run
  [2, 0, 0.1] # pullback coast
]
eject_mcs = MCS(eject_motor_adr, eject_motor_sequence)


def card_sort(basket):
  dir = basket & 1
  if (basket & 2) == 0:
    sorter_motor_sequence = [
      [dir, 7, 0.04],  # run
      [3, 0, 0.3],       # break
      [1-dir, 7, 0.03],  # opposite run
      [3, 0, 0.1],       # break
      [dir, sorter_motor_basket_0_1_speed, sorter_motor_basket_0_1_time], # run
      [2, 0, 0.3],       # coast
      [1-dir, 7, 0.03],  # second attempt, opposite run
      [3, 0, 0.1],       # break
      [dir, sorter_motor_basket_0_1_speed, sorter_motor_basket_0_1_time], # run
      [2, 0, 0.1]       # coast
    ]    
  else:
    sorter_motor_sequence = [
      [dir, sorter_motor_basket_2_3_speed, sorter_motor_basket_2_3_time], # run
      [2, 0, 0.1]       # coast
    ]
  return sorter_motor_sequence

# https://stackoverflow.com/questions/46390779/automatic-white-balancing-with-grayworld-assumption

def white_balance(img):
    result = cv2.cvtColor(img, cv2.COLOR_BGR2LAB)
    avg_a = np.average(result[:, :, 1])
    avg_b = np.average(result[:, :, 2])
    result[:, :, 1] = result[:, :, 1] - ((avg_a - 128) * (result[:, :, 0] / 255.0) * 1.1)
    result[:, :, 2] = result[:, :, 2] - ((avg_b - 128) * (result[:, :, 0] / 255.0) * 1.1)
    result = cv2.cvtColor(result, cv2.COLOR_LAB2BGR)
    return result

def remove_barrel_distortion(img):
	bordersize = 32
	src = cv2.copyMakeBorder(
	    img,
	    top=bordersize,
	    bottom=bordersize,
	    left=bordersize,
	    right=bordersize,
	    borderType=cv2.BORDER_CONSTANT,
	    value=[0, 0, 0]
	)

	# remove the barrel distortion

	width  = src.shape[1]
	height = src.shape[0]

	distCoeff = np.zeros((4,1),np.float64)

	k1 = -1.2e-5; # negative to remove barrel distortion
	k2 = 0.0;
	p1 = 0.0;
	p2 = 0.0;

	distCoeff[0,0] = k1;
	distCoeff[1,0] = k2;
	distCoeff[2,0] = p1;
	distCoeff[3,0] = p2;

	# assume unit matrix for camera
	cam = np.eye(3,dtype=np.float32)

	cam[0,2] = width/2.0  # define center x
	cam[1,2] = height/2.0 # define center y
	cam[0,0] = 10.        # define focal length x
	cam[1,1] = 10.        # define focal length y

	# here the undistortion will be computed
	dst = cv2.undistort(src,cam,distCoeff)
	return dst

def clean_str(s):
  t = ''
  for c in s:
    if c >= 'A' and c <=  'Z':
      t += c
    elif c >= 'a' and c <= 'z':
      t+= c
    elif c == ' ':
      t+= c
    elif c >= ' ':
      t+='_'
  return t
  

def read_file(filename):
	f = open(filename)
	s = f.read()
	f.close()
	return s

def append_to_file(filename, s):
	f = open(filename, "a")
	f.write(s)
	f.close()

def read_json(filename):
  f = io.open(filename, "r", encoding=None)
  obj = json.load(f)
  f.close()
  return obj
  
def file2json(filename):
  f = io.open(filename, "rb", encoding=None)
  encoded_string = base64.b64encode(f.read()).decode('ascii')
  f.close()
  return json.dumps(encoded_string)
  

def cam_capture(cam, imagename, fullimname):
  rawCapture = PiRGBArray(cam)
  #camera.capture('image.jpg')
  cam.capture(rawCapture, format="bgr")
  # remove the barrel distortion of the raspi cam
  #image = remove_barrel_distortion(white_balance(rawCapture.array))
  image = remove_barrel_distortion(rawCapture.array)
  # write a low quality picture of the scanned card
  cv2.imwrite(fullimname, image,[cv2.IMWRITE_JPEG_QUALITY, jpeg_full_image_quality, cv2.IMWRITE_JPEG_LUMA_QUALITY, jpeg_full_image_quality]);
  #image = rawCapture.array;
  image = image[0:140, 0:1279]
  # https://stackoverflow.com/questions/9480013/image-processing-to-improve-tesseract-ocr-accuracy
  image = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
  #(thresh, blackAndWhiteImage) = cv2.threshold(image, 120, 255, cv2.THRESH_BINARY)

  kernel = np.ones((3, 3), np.uint8)
  img = cv2.dilate(image, kernel, iterations=1)
  img = cv2.erode(img, kernel, iterations=1)	
  #cv2.imwrite('pre_'+imagename, img);
  img = cv2.medianBlur(img, 3)
  #cv2.imwrite('pre2_'+imagename, img);  
  # the last argument: 0=lot of noise, 2: little bit noise, 4: no noise any more
  #img = cv2.adaptiveThreshold(img, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C, cv2.THRESH_BINARY, 63, 2)
  
  cv2.imwrite('raw_'+imagename, image);
  cv2.imwrite(imagename, img);
  #cv2.imwrite('image_bw_'+str(i)+'.jpg', blackAndWhiteImage);
  # tesseract --dpi 500 --psm 6 image_g_XX.jpg stdout

def avg(list):
    return sum(list) / len(list)

# return the result from tesseract
def get_ocr_card_name(imagefile):
  # execute tesseract
  os.system("tesseract --dpi 500 --psm 6 " + imagefile +" out txt")  # write to out.txt

  # character based updates
  ocr_result = read_file('out.txt')
  ocr_result = ocr_result.replace(chr(8212), "")          # big dash, created by tesseract
  ocr_result = ocr_result.replace("_", " ")          # replace underscore with blank
  ocr_result = ocr_result.replace("~", "")          # remove tilde
  ocr_result = ocr_result.replace(".", " ")          # replace  dot with blank
  ocr_result = ocr_result.replace("@", "")          # remove @
  
  # maybe add a bonus for the number of lower case letters
  # find the line which contains the card name with highest probability
  ocr_lines = ocr_result.split("\n")      # split into lines
  ocr_lines = list(map(lambda s: s.split(" "), ocr_lines))        # split lines into words
  ocr_lines = list(map(lambda l : list(filter(lambda s: len(s) > 2, l)), ocr_lines  )) # remove words with 1 or 2 chars
  ocr_lines = list(filter(lambda l : len(l) > 0, ocr_lines))       # remove lines which are now empty
  ocr_hist = list(map(lambda l : list(map(lambda s: len(s), l)), ocr_lines))  # replace all strings by their string length
  ocr_hist = list(map(lambda l : avg(l), ocr_hist))       # calculate the average word size
  ocr_name = ""
  if len(ocr_hist) > 0:
    line_index = ocr_hist.index(max(ocr_hist))                  # get the line index with the highest average word size
    ocr_name = " ".join(ocr_lines[line_index])
    
    # log some data to the log file
    append_to_file(args.log, str(ocr_lines)+"\n")
    append_to_file(args.log, str(ocr_hist)+": "+ ocr_name + "\n")
    
  return ocr_name
  
# return a vector with the internal card id, the card name and the distance to the tesseract name
def find_card_old(carddic, ocr_name):
  if ocr_name == "":
    return [-1, 9999, ""]
  t = { 
  8209: 45, 8211:45, # convert dash
  48: 111, 79: 111, # convert zero and uppercase O to small o
  211: 111, 212: 111, 214: 111, # other chars similar to o
  242: 111, 243: 111, 244: 111, 245: 111, 246: 111, # other chars similar to o
  959:111, 1086:111, 8009:111, 1054:111,    # other chars similar to o
  73:105, 74:105, 106:105, 108:105, 124:105, # convert upper i, upper j, small j, small l and pipe symbol to small i
  161:105, 205:105, 206:105, 236:105, 237:105, 238:105, 239:105, 1575:105,  # convert other chars to i
  192: 65, 193: 65, 194: 65, 196: 65, 1040:65, 1044:65,         # upper A
  200: 69, 201: 69, 202: 69, 1045:69,   # upper E
  85:117,  # convert upper U to small u
  218: 117, 220: 117,  # other conversions to small u
  249: 117, 250: 117, 251: 117, 252: 117, # other conversions to small u
  956: 117, 1094: 117,
  224: 97, 225: 97, 226: 97, 227: 97, 228: 97, 229: 97, # small a conversion
  232: 101, 233: 101, 234: 101, 235: 101 # small e conversion
  }

  d = 999
  dmin = 999
  smin = ""
  n = ocr_name.translate(t)
  for c in carddic:
    #d = jellyfish.levenshtein_distance(c.translate(t), n)
    d = jellyfish.levenshtein_distance(c, ocr_name)
    if dmin > d:
      dmin = d
      smin = c
      print(c + "/"+ ocr_name+" "+str(d))
      #print(c.translate(t) + "/"+ ocr_name.translate(t))
      
  append_to_file(args.log, "--> "+ smin + " (" + str(carddic[smin]) + ")\n")
  return [carddic[smin], smin, dmin]

def find_card(ocr_name):
  print(f"find_card: search {ocr_name}")
  f = open(pipename, "w")
  f.write(json.dumps(cardname))
  f.close()
  f = open(pipename, "r")
  v = json.load(f)
  f.close();
  print(f"find_card: result {v[1]}")
  append_to_file(args.log, "find_card "+ ocr_name + " --> " + v[1] + ")\n")
  print(v)


def eval_cond(cond, prop):
  tc = prop["tc"]       # Creature
  ts = prop["ts"]       # Sorcery
  ti = prop["ti"]       # Instant
  ta = prop["ta"]       # Artefact
  tl = prop["tl"]       # Land
  te = prop["te"]       # Enhancement
  tp = prop["tp"]       # Planeswalker
  cmc = prop["c"]
  r = prop["r"]                 # rarity 0=common, 1=uncommon, 2=rare, 3=mythic
  try:
    cw = "W" in prop["i"]
    cb = "B" in prop["i"]
    cr = "R" in prop["i"]
    cg = "G" in prop["i"]
    cu = "U" in prop["i"]
  except:
    cw = False
    cb = False
    cr = False
    cg = False
    cu = False
    
  #print('cmc='+str(cmc))
  return eval(cond)

def get_basket_number(prop):
  if eval_cond(args.b0c, prop):
    print(args.b0c + ' --> 0')
    return 0
  if eval_cond(args.b1c, prop):
    print(args.b1c + ' --> 1')
    return 1
  if eval_cond(args.b2c, prop):
    print(args.b2c + ' --> 2')
    return 2
  print('No matching condition --> 3')
  return 3
  
  
def sort_machine():


  camera = PiCamera()
  camera.start_preview()
  camera.exposure_mode = 'night'
  camera.brightness = 60	# default: 50
  camera.contrast = 100     # default: 0
  camera.rotation = 90
  camera.resolution = (1024,1280)

  card_dic = read_json('mtg_card_dic.json')
  card_prop = read_json('mtg_card_prop.json')

  m = eject_mcs.start()
  while m:
    m = eject_mcs.next();
      
  for i in range(args.r):

    t = time.time()
    strdt = datetime.now().strftime("%Y_%m_%d_%H%M%S")
    cam_capture(camera, 'image.jpg', strdt+'.jpg')
    t_cam = time.time()
    
    if args.key != '.':
      ocr_name = clean_str(ocr_space_file(filename='image.jpg', api_key=args.key))
      if ocr_name == quit_word:
        print("no card visible")
        break
      t_ocr = time.time()
      #cardv = find_card(card_dic, ocr_name)
      cardv = find_card(ocr_name)
      t_find = time.time()
      print( cardv[1] )
    else:
      ocr_name = get_ocr_card_name('image.jpg')
      if ocr_name == quit_word:
        print("no card visible")
        break
      t_ocr = time.time()
      #cardv = find_card(card_dic, ocr_name)
      cardv = find_card(ocr_name)
      t_find = time.time()
   
    if cardv[0] >= 0:
      os.rename(strdt+'.jpg', strdt+'_'+clean_str( cardv[1] )+'.jpg')
      #print( cardv[1] )
      #print( clean_str( cardv[1] ))
      basket_number = get_basket_number(card_prop[cardv[0]])
    else:
      basket_number = 3
      
    #card_sort(basket_number)

    sort_mcs = MCS(sorter_motor_adr, card_sort(basket_number))
    # m = sort_mcs.start()
    # while m:
     #  m = sort_mcs.next();      

    m1, m2 = sort_mcs.start(), eject_mcs.start()
    while m1 or m2:
      m1, m2 = sort_mcs.next(), eject_mcs.next()
    
    append_to_file(args.log, "cam: "+str(t_cam-t)+', ocr: ['+ocr_name+']/'+str(t_ocr - t_cam)+', find: '+str(t_find-t_ocr)+', basket: '+str(basket_number)  )
    
  camera.stop_preview()

if args.c == '':
  print("use -h to read the commandline help page");
elif args.c == 'help':
  print("use -h to read the commandline help page");
elif args.c == 'sort':
  sort_machine();
elif args.c == 'eb':
  for i in range(args.r):
    m = eject_mcs.start()
    while m:
      m = eject_mcs.next();
    sort_mcs = MCS(sorter_motor_adr, card_sort(args.b))
    m = sort_mcs.start()
    while m:
      m = sort_mcs.next();      
elif args.c == 'em':
  motor_run(eject_motor_adr,args.ems,args.emd)
  time.sleep(args.emt/1000.0)
  motor_break(eject_motor_adr)
elif args.c == 'sm':
  motor_run(sorter_motor_adr,args.sms,args.smd)
  time.sleep(args.smt/1000.0)
  motor_break(sorter_motor_adr)
elif args.c == 'tc':
  card_prop = read_json('mtg_card_prop.json')
  print(args.b0c)
  print(eval_cond(args.b0c, card_prop[0]));
  print(args.b1c)
  print(eval_cond(args.b1c, card_prop[0]));
  print(args.b2c)
  print(eval_cond(args.b2c, card_prop[0]));
elif args.c == 'cam':
  camera = PiCamera()
  camera.start_preview()
  camera.exposure_mode = 'night'
  camera.brightness = 60	# default: 50
  camera.contrast = 100     # default: 0
  camera.rotation = 90
  camera.resolution = (1024,1280)
  #camera.resolution = (768,1024)
  cam_capture(camera, 'image.jpg', 'cam_image.jpg')
  ocr_name = get_ocr_card_name('image.jpg')
  print(ocr_name)  
  

Echo �T�[�r�X�E�N���C�A���g
===========================

���̃v���O�����͒P���ɒl�̎󂯓n�����s�����ƂŁA���܂��܂ȓ���m�F����
���邱�Ƃ�ړI�Ƃ��Ă��܂��B
���܂��܂Ȍ^���ׂ���������悤�ɂ��āA�܂��AHello�n�T���v���Ƃ͋t�ɃG
���[�`�F�b�N���Ȃ�ׂ��s�Ȃ��A�e��API�g���@�\�̃f�o�b�O�Ɏg���悤�ȏ�
���𐷂荞��ł���܂��B
�ŏI�I�ɂ́A���ݐڑ���������A�����[�X�O�̃��O���b�V�����e�X�g�����Ɏ�
�������Ďg����悤�ɂ������ƍl���Ă��܂��B

* EchoClient
  Echo �N���C�A���g�v���O�����ł��B

  �g�p���@: EchoClient [options] test# data [data2 [data3 ..]]
  �I�v�V����:
  -h --help              �w���v���b�Z�[�W�\��
  -v --verbose           �璷���[�h�BSOAP���b�Z�[�W��\���B
  -d --debug             �f�o�b�O���[�h�B�f�o�b�O���b�Z�[�W��\���B
  -s --endpoint=ENDPOINT EchoService�̃G���h�|�C���g
                         (default: http://localhost/cgi-bin/EchoService.cgi)
  -u --http-user=USER    HTTP�F�؂̃��[�U��
                         (default: env ECHO_HTTP_USER)
  -p --http-passwd=PASS  HTTP�F�؂̃p�X���[�h
                         (default: env ECHO_HTTP_PASS)
  -t --http-type=AUTH    HTTP�F�؃^�C�v (�f�t�H���g:0=���ł�,
                         1=�x�[�V�b�N, 2=�_�C�W�F�X�g)
  -S --sslver=VER        �g�p����SSL�o�[�W�������w��
                         (default:0=ALL, 1=SSLv2,2=SSLv3,4=TLSv1)
  -V --verify=LEVEL      SSL�T�[�o�F�؃��x�����w��
                         (default:0=�C�ɂ��Ȃ�, 1=����)
  --ca_dir=DIR           CA�ؖ����̊܂܂��f�B���N�g�����w��
  --ca_file=FILE         CA�ؖ����̃t�@�C�������w��
  --certchain_file=FILE  �N���C�A���g�F�؃`�F�C���̃t�@�C����
  --privkey_file=FILE    �N���C�A���g�F�؂̔閧���̃t�@�C����

 test# = ���\�b�h��
 test:0 = Echo
  �����Ă������b�Z�[�W�����̂܂ܕԂ��B����������

 test:1 = echoString
  ��������w�肵�đ���B

  ��$ EchoClient 1 foo

 test:2 = echoStringArray
  ������̔z����w�肵�đ���B����������

  ��$ EchoClient 2 foo bar

 test:3 = echoInteger
  �������w�肵�đ���B

  ��$ EchoClient 3 1234567890

 test:4 = echoIntegerArray
  �����̔z����w�肵�đ���B����������

  ��$ EchoClient 3 1 2 3 4 5 6 -1 -2 -3

 test:5 = echoFloat
  ���������_�����w�肵�đ���B

  ��$ EchoClient 5 3.141592

 test:6 = echoFloatArray
  ���������_���̔z����w�肵�đ���B����������

  ��$ EchoClient 6 1.1 1.2 1.3 1.4 1.5

 test:7 = echoStruct
  �\���̂̃f�[�^���w�肵�đ���B����������

  ��$ EchoClient 7 filename?

 test:8 = echoStructArray
  �\���̂̃f�[�^�̔z����w�肵�đ���B����������

  ��$ EchoClient 8 filename1 filename2 filename3�H

 test:9 = echoVoid
  void�𑗂�B����������

  ��$ EchoClient 9

 test:10 = echoBase64
  �w�肵���t�@�C���̓��e��Base64�G���R�[�f�B���O���đ���B

  ��$ EchoClient 10 �t�@�C��

 test:11 = echoDate
  �w�肵�����t���𑗂�B�w�肵�Ȃ��ꍇ�A���ݎ����B

  ��$ EchoClient 11
  ��$ EchoClient 11 "Fri Nov  7 11:50:23 JST 2003"

 test:12 = echoDouble
  �{���x���������_�����w�肵�đ���B

  ��$ EchoClient 5 3.1415926535897932384626433832795029

* EchoService
  �W�����o�͌^�� Echo �T�[�r�X�v���O����

* EchoService.cgi
  CGI�^�� Echo �T�[�r�X�v���O����

�l����x�^�����킹���ϐ��ɓ���Ă��珈������̂ŁA�^�ƒl������Ȃ��ƃG
���[�ɂȂ�\��������܂��B
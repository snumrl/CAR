# Learning a family of motor skills from a single motion clip

### install 

> 1. cuda 10.2 설치
> 2. CAR 폴더에서 ./install.sh
> 3. export PYTHONPATH=$PYTHONPATH:/PATH/TO/CAR/network 를 터미널 설정 파일(bashrc, zshrc 등)에 추가

### render

> 1. cd ./build/render
> 2. ./render --type=sim --ref=bvh_name.bvh --network=network_name/network-0
>> * 이 외의 argument 옵션은 ./render/main.cpp 파일 참고

### train

> 1. cd ./network
> 2. python3 ppo.py --ref=bvh_name.bvh --test_name=test_name --pretrain=output/test_name/network-0
>> * pretrain이 있을 경우 test_name은 같은 이름으로 설정
>> * 이 외의 argument 옵션은 network/ppo.py 파일 참고

### SendToUE

>> 주요 변경내용
> 1. MotionWidget.cpp
>  - socket 관련 함수들 include
>  - mJointsUEOrder : joint_name 추가
>  - SetFrame : mSkel_reg SetPosition하고나서 sendMotion() 추가
>  - 추가된 함수들 : connectionOpen() / connectionClose() / sendMotion() / UEconnect() / UEclose() : 추가 변경 필요 X
>                 getCharacterTransformsForUE : 추가 변경필요 X / (mSkel_reg의 모션 활용) / skeleton 외 추가 object 필요시 수정하는 부분
> 2. MainWindow.cpp
> - UEconnect와 UEclose 버튼 추가
> 3. Controller.cpp
> - Eigen::Isometry3d Controller::getLocalSpaceTransform(const dart::dynamics::SkeletonPtr& Skel) 함수 추가
               
>> 윈도우에서 실행시킬 코드
> - https://github.com/y0ngw00/UEconnection.git
> 1. TCPclient.cpp의 clientTalk 함수에서 리눅스의 IP주소를 inet_addr 뒷부분에 수정
> 2. FastIKCharacter.cpp
>  - bone_name : skeleton joint name list. pelvis는 UE캐릭터 /  Hips는 mixamo캐릭터
>  - void AFastIKCharacter::Tick : sendAndReceiveData에서 받는 데이터 양 : (NUM_JOINT+1)4x4 matrix + ext_variable.
>  - ext_variable = 캐릭터 외에 추가적으로 보내는 변수(헤더파일에서 선언함)
>  - void AFastIKCharacter::updateBoneTransforms() : ext_variable 있는 만큼 내용 추가(현재는 0)

>> 실행 방식
> 0. render_qt 실행 방식은 기존과 같음
> 1. Qt에서 원하는 goal parameter를 정한후 ueconnect를 누른 뒤 play(UE에서 받기 전까지는 재생 안됨)
> 2. 코드 빌드 시 뜨는 UE에서 플레이 버튼을 누르면 실행됨.

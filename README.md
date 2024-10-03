# ECNU OSLab 2024

## 简介

这是一份OS实验说明, 实验目标是基于C语言和RISC-V体系结构从零开始设计实现一个简单OS内核  

实验过程中建议你参考xv6-riscv的实现 (同学们正在使用的xv6版本):

**git://g.csail.mit.edu/xv6-labs-2020** 中的 **util** 分支的 **kernel** 文件夹下的文件  

## 和正在做的OS实验有何区别

- 目前的OS实验本质是在一个基础的能运行的内核上做修改完善以通过测试  

    你做的修改依赖于已有代码，通常不需要太多行代码就能达成目的  

    每次新的实验都基于新的分支，不保留之前实验的修改

- 这个OS实验并不基于任何已经能跑的内核，你需要从头开始一步步搭建起内核  

    这意味着你调用的几乎所有函数都是自己写的，实验结束时你至少要产出2000+行代码  

    实验具有连续性，你这次写的代码后面还会用到，所以要对自己的代码负责  

    听起来更具挑战性，但是也不用太担心，你可以随意查看xv6的实现作为指导  

- 三种类型的OS实验:

    锦上添花型: 基于一个完整的能运行的内核，增加新的功能或特性，如xv6  

    完型填空型: 内核基本完整，但是在某些函数中挖去了一部分关键代码等你来填，如chcore  

    从头开始型: 给出设计框架和各阶段目标，从头开始一步步实现一个内核  

## 从头开始“造轮子”有什么意义

已知现有的OS内核都是千万行级别的代码，完全不是一个人能搞定的，造这样一个简单的轮子有什么用呢？  

1. 首先，作为这个领域的初学者，要想深刻体会OS内核的运行原理，光阅读别人的代码是不够的  

    最好在读完后能动手写一个简单的demo，这个demo不需要复杂的算法和优异的性能，只需要体现最基础最核心的设计思想即可  

    每一个优秀的车辆工程师在小时候总是拆过和装过许多玩具汽车  

2. 其次，跳脱出操作系统这个大背景，动手写内核也是提高系统级编程能力的好机会  

    可以单纯把它作为一个普通的项目去开发， 了解如何编写和管理一个多文件的大型项目

    真实的软件开发不可能像算法比赛一样提交单个文件就够了

## 前期需要做哪些准备

1. 基础知识: 建议超过课程进度，尽快学习完OS的基本知识，不用很深入但是要有了解  

2. 环境配置: 出于减轻大家负担的考虑，我们仍然沿用目前OS实验的环境，不做大的变化  

3. 常用工具: vscode + git + markdown  

4. 编译纠错: Makefile + gcc + gdb + 编译运行基本知识  

5. 其他知识: C语言语法复习(尤其是多文件组织) + qemu简单了解 + risc-v简单了解  

## 你能获得的帮助

1. 助教会为每一次实验搭建框架，并提供必要的代码（通常是硬件相关和Makefile）  

2. 助教会为每一次实验编写实验指导书，说明实验的目标并介绍必要知识  

3. xv6的资料非常多，代码也是开源的，你应该好好利用起来  

4. 我们会建立一个讨论群，有问题可以发到群里，大家互帮互助  

## 你需要具备的素养

1. 热爱OS并愿意付出相当比例的时间投入这个实验，混子劝退  

2. 能够独立思考和独立解决问题，写内核遇到的问题会比目前的实验多得多，大多数时间只能靠自己  

3. 对系统底层原理有好奇心，不希望做一个“调包侠”，希望自己动手实现自己调用的函数  

4. 能够静下心 Code and Debug，平静面对失败，永不言弃

## 让我们开始：建立仓库

1. 在线上建立代码托管仓库(小白建议使用gitee，国产的没有网络问题)，git clone到本地  

2. 在本地使用Markdown编写README文件，简单介绍你的项目（如内核的名字，目的，成员等信息）

3. 练习和熟悉 vscode + git + markdown，后面需要长期用到  
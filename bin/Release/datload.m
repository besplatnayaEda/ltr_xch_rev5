clc
clear all
close all

s = load('data/s7478_0001.dat');

t = linspace(0,length(s(:,1))/14648.4375,length(s(:,1)));

figure(1)
hold on
%plot(t,s(:,1), 'r','LineWidth', 2)
%plot(t,s(:,2), 'g','LineWidth', 2)
%plot(t,s(:,3), 'b','LineWidth', 2)
%plot(t,s(:,4), 'm','LineWidth', 2),grid

figure(2)
hold on
plot(t,s(:,5), 'LineWidth', 2)
%plot(t,s(:,6), 'r','LineWidth', 2),grid
hold off
%
%Fs = 14648.4375;
%L = length(s(:,1));
%f = (Fs/2*linspace(0,1,L/2));
%y = abs(fft(s(:,1),L)/L);
%
%figure(3)

%plot(f,20*log10(y(1:length(f))),'LineWidth', 2)
%grid on